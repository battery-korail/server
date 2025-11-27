
from flask import Flask, request, jsonify
from flask_cors import CORS
import psycopg2
from psycopg2.extras import RealDictCursor
import os
from dotenv import load_dotenv
import time
import logging
import sys
import threading
import paho.mqtt.client as mqtt
from flask_socketio import SocketIO


load_dotenv()
app = Flask(__name__)
CORS(app, resources={r"/*": {"origins": "*"}})

# ====== Socket.IO ======
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="threading")

# ====== Logging ======
logging.basicConfig(stream=sys.stdout, level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
logger = logging.getLogger("battery_backend")

# ====== PostgreSQL 연결 ======
def get_conn():
    return psycopg2.connect(
        host=os.getenv("PG_HOST"),
        port=int(os.getenv("PG_PORT")),
        dbname=os.getenv("PG_DB"),
        user=os.getenv("PG_USER"),
        password=os.getenv("PG_PASS")
    )

# ====== DB 스키마 보장 ======
def ensure_tables():
    try:
        conn = get_conn()
        cur = conn.cursor()
        cur.execute("""
            CREATE TABLE IF NOT EXISTS dp_saved (
                id SERIAL PRIMARY KEY,
                sg DOUBLE PRECISION NOT NULL,
                created_at TIMESTAMP WITH TIME ZONE DEFAULT now()
            );
        """)
        # level 컬럼이 없으면 추가
        cur.execute("""
            ALTER TABLE dp_saved
            ADD COLUMN IF NOT EXISTS level DOUBLE PRECISION;
        """)
        conn.commit()
        cur.close()
        conn.close()
    except Exception as e:
        logger.exception("DB init error: %s", e)

ensure_tables()

# ====== MQTT 설정 ======
MQTT_BROKER = os.getenv("MQTT_BROKER", "mqtt")
MQTT_PORT = int(os.getenv("MQTT_PORT", 1883))
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "stm/dp")

# 최신 데이터 캐시
latest_data = {
    "sg": 0.0,
    "samples": 0
}
last_mqtt_received_at = 0.0
mqtt_client = None

# MQTT 콜백
def on_connect(client, userdata, flags, rc):
    logger.info(f"[MQTT] Connected with result code {rc}")
    client.subscribe(MQTT_TOPIC)
    logger.info(f"[MQTT] Subscribed to topic: {MQTT_TOPIC}")

def on_message(client, userdata, msg):
    global latest_data, last_mqtt_received_at
    import json
    try:
        payload_text = msg.payload.decode()
        logger.info("[MQTT] Received: %s", payload_text)
        payload = json.loads(payload_text)
        
        # 수신 포맷 유연 처리: level은 'level' 또는 'samples' 키로 들어올 수 있음
        if "sg" in payload:
            latest_data["sg"] = float(payload["sg"])
        if "level" in payload:
            latest_data["samples"] = float(payload["level"])
        elif "samples" in payload:
            latest_data["samples"] = float(payload["samples"])

        if "sg" in payload or "level" in payload or "samples" in payload:
            last_mqtt_received_at = time.time()
            # 웹에 push
            logger.info("[MQTT] Parsed sg=%.3f level=%.3f", float(latest_data["sg"]), float(latest_data["samples"]))
            socketio.emit("batteryUpdate", {"gravity": latest_data["sg"], "level": latest_data["samples"]})
    except Exception as e:
        logger.exception("[MQTT] payload parse error: %s", e)
        
def on_disconnect(client, userdata, rc):
    logger.warning("[MQTT] Disconnected (rc=%s)", rc)

# MQTT 클라이언트 시작 (백그라운드 스레드)
def start_mqtt():
    try:
        global mqtt_client
        # Explicit protocol for compatibility with paho-mqtt 1.x
        mqtt_client = mqtt.Client(client_id="flask_server", protocol=mqtt.MQTTv311, transport="tcp")
        mqtt_client.enable_logger(logger)
        mqtt_client.on_connect = on_connect
        mqtt_client.on_message = on_message
        mqtt_client.on_disconnect = on_disconnect
        mqtt_client.reconnect_delay_set(min_delay=1, max_delay=30)
        logger.info(f"[MQTT] Connecting to {MQTT_BROKER}:{MQTT_PORT}")
        rc = mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
        logger.info(f"[MQTT] connect() returned rc={rc}")
        # Block in this thread to keep MQTT loop alive
        mqtt_client.loop_forever()
    except Exception as e:
        logger.exception("MQTT thread error: %s", e)

mqtt_thread = threading.Thread(target=start_mqtt)
mqtt_thread.daemon = True
mqtt_thread.start()

# ====== REST API ======
@app.route("/dp")
def get_dp_log():
    try:
        limit = int(request.args.get("limit", 50))
        conn = get_conn()
        cur = conn.cursor(cursor_factory=RealDictCursor)
        cur.execute("SELECT * FROM dp_log ORDER BY id DESC LIMIT %s;", (limit,))
        rows = cur.fetchall()
        cur.close()
        conn.close()
        return jsonify(rows[::-1])
    except Exception as e:
        print("Error in /dp:", e)
        return jsonify({"error": str(e)}), 500

@app.route("/dp/save", methods=["POST"])
def save_dp():
    try:
        sg_value = latest_data.get("sg", 0.0)
        level_value = latest_data.get("samples", 0.0)
        if sg_value is None:
            return jsonify({"error": "저장할 데이터가 없습니다."}), 400

        def do_insert():
            conn = get_conn()
            cur = conn.cursor()
            cur.execute(
                "INSERT INTO dp_saved (sg, level) VALUES (%s, %s) RETURNING id, sg, level, created_at;",
                (sg_value, level_value)
            )
            saved_row = cur.fetchone()
            conn.commit()
            cur.close()
            conn.close()
            return saved_row

        try:
            saved = do_insert()
        except Exception as inner_e:
            # 테이블이 없으면 생성 후 한 번 재시도
            if "dp_saved" in str(inner_e):
                ensure_tables()
                saved = do_insert()
            else:
                raise

        return jsonify({
            "id": saved[0],
            "dp_pa": saved[1],
            "level": saved[2],
            "created_at": saved[3].isoformat()
        })
    except Exception as e:
        print("Error in /dp/save:", e)
        return jsonify({"error": str(e)}), 500

@app.route("/dp/saved")
def get_saved_values():
    try:
        page = int(request.args.get("page", 1))
        per_page = int(request.args.get("per_page", 10))
        order = request.args.get("order", "desc").lower()
        date_str = request.args.get("date", "").strip()
        # 정렬 파라미터 화이트리스트
        order_sql = "DESC" if order not in ["asc", "desc"] else order.upper()
        offset = (page - 1) * per_page

        def query():
            conn = get_conn()
            cur = conn.cursor(cursor_factory=RealDictCursor)
            where_clauses = []
            params = []
            if date_str:
                # YYYY-MM-DD 문자열 기준으로 날짜 필터
                where_clauses.append("created_at::date = %s")
                params.append(date_str)
            where_sql = f"WHERE {' AND '.join(where_clauses)}" if where_clauses else ""

            # created_at 기준 정렬(동일 시 id 보조 정렬)
            cur.execute(
                f"""
                SELECT id, sg AS dp_pa, level, created_at
                FROM dp_saved
                {where_sql}
                ORDER BY created_at {order_sql}, id {order_sql}
                LIMIT %s OFFSET %s;
                """,
                (*params, per_page, offset)
            )
            rows_local = cur.fetchall()
            cur.execute(
                f"SELECT COUNT(*) FROM dp_saved {where_sql};",
                tuple(params)
            )
            total_local = cur.fetchone()["count"]
            cur.close()
            conn.close()
            return rows_local, total_local

        try:
            rows, total = query()
        except Exception as inner_e:
            # 테이블 미존재 시 생성 후 빈 결과 반환 또는 재시도
            if "dp_saved" in str(inner_e):
                ensure_tables()
                # 생성 직후에는 데이터가 없을 수 있으므로 빈 결과 반환
                rows, total = [], 0
            else:
                raise

        return jsonify({
            "data": rows,
            "page": page,
            "per_page": per_page,
            "total": total,
            "total_pages": (total + per_page - 1) // per_page if per_page > 0 else 1
        })
    except Exception as e:
        print("Error in /dp/saved:", e)
        return jsonify({"error": str(e)}), 500

# ====== Socket.IO 이벤트 (클라이언트 접속 확인용) ======
@socketio.on("connect")
def handle_connect():
    logger.info("Client connected")
    # 연결 시 최신 데이터 전송
    socketio.emit("batteryUpdate", {"gravity": latest_data["sg"], "level": latest_data["samples"]})
    logger.info("[Socket.IO] Emitting: %s", {"gravity": latest_data["sg"], "level": latest_data["samples"]})


@socketio.on("disconnect")
def handle_disconnect():
    logger.info("Client disconnected")

# ====== Health ======
@app.route("/health/mqtt")
def health_mqtt():
    return jsonify({
        "broker": f"{MQTT_BROKER}:{MQTT_PORT}",
        "topic": MQTT_TOPIC,
        "latest_data": latest_data,
        "last_mqtt_received_at": last_mqtt_received_at
    })

if __name__ == "__main__":
  socketio.run(app, host="0.0.0.0", port=5000, allow_unsafe_werkzeug=True)

