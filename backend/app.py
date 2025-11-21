import eventlet
eventlet.monkey_patch()
from flask import Flask, request, jsonify
from flask_cors import CORS
import psycopg2
from psycopg2.extras import RealDictCursor
import os
from dotenv import load_dotenv
import threading
import paho.mqtt.client as mqtt
from flask_socketio import SocketIO


load_dotenv()
app = Flask(__name__)
CORS(app, resources={r"/*": {"origins": "*"}})

# ====== Socket.IO ======
socketio = SocketIO(app, cors_allowed_origins="*")



# ====== PostgreSQL 연결 ======
def get_conn():
    return psycopg2.connect(
        host=os.getenv("PG_HOST"),
        port=int(os.getenv("PG_PORT")),
        dbname=os.getenv("POSTGRES_DB"),
        user=os.getenv("POSTGRES_USER"),
        password=os.getenv("POSTGRES_PASSWORD")
    )

# ====== MQTT 설정 ======
MQTT_BROKER = os.getenv("MQTT_BROKER", "mqtt")
MQTT_PORT = int(os.getenv("MQTT_PORT", 1883))
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "stm/dp")

# 최신 데이터 캐시
latest_data = {
    "sg": 0.0,
    "samples": 0
}

# MQTT 콜백
def on_connect(client, userdata, flags, rc):
    print(f"[MQTT] Connected with result code {rc}")
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    global latest_data
    import json
    try:
        print("[MQTT] Received:", msg.payload.decode()) 
        payload = json.loads(msg.payload.decode())
        print(payload)
        
        if "sg" in payload and "samples" in payload:
            latest_data["sg"] = float(payload["sg"])
            latest_data["samples"] = int(payload["samples"])
            # 웹에 push
            print(latest_data)
            socketio.emit("batteryUpdate", {"gravity": latest_data["sg"], "level": latest_data["samples"]})
    except Exception as e:
        print(f"[MQTT] payload parse error: {e}")

# MQTT 클라이언트 시작 (백그라운드 스레드)
def start_mqtt():
    client = mqtt.Client("flask_server")
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_forever()

mqtt_thread = threading.Thread(target=start_mqtt)
mqtt_thread.daemon = True
mqtt_thread.start()

# ====== REST API ======
@app.route("/dp")
def get_dp_log():
    limit = int(request.args.get("limit", 50))
    conn = get_conn()
    cur = conn.cursor(cursor_factory=RealDictCursor)
    cur.execute("SELECT * FROM dp_log ORDER BY id DESC LIMIT %s;", (limit,))
    rows = cur.fetchall()
    cur.close()
    conn.close()
    return jsonify(rows[::-1])

@app.route("/dp/save", methods=["POST"])
def save_dp():
    try:
        sg_value = latest_data.get("sg", 0.0)
        if sg_value is None:
            return jsonify({"error": "저장할 데이터가 없습니다."}), 400

        conn = get_conn()
        cur = conn.cursor()
        cur.execute(
            "INSERT INTO dp_saved (sg) VALUES (%s) RETURNING id, sg, created_at;",
            (sg_value,)
        )
        saved = cur.fetchone()
        conn.commit()
        cur.close()
        conn.close()

        return jsonify({
            "id": saved[0],
            "dp_pa": saved[1],
            "created_at": saved[2].isoformat()
        })
    except Exception as e:
        print("Error in /dp/save:", e)
        return jsonify({"error": str(e)}), 500

@app.route("/dp/saved")
def get_saved_values():
    page = int(request.args.get("page", 1))
    per_page = int(request.args.get("per_page", 10))
    offset = (page - 1) * per_page

    conn = get_conn()
    cur = conn.cursor(cursor_factory=RealDictCursor)
    cur.execute(
        "SELECT id, sg AS dp_pa, created_at FROM dp_saved ORDER BY id DESC LIMIT %s OFFSET %s;",
        (per_page, offset)
    )
    rows = cur.fetchall()

    cur.execute("SELECT COUNT(*) FROM dp_saved;")
    total = cur.fetchone()["count"]

    cur.close()
    conn.close()
    return jsonify({
        "data": rows,
        "page": page,
        "per_page": per_page,
        "total": total,
        "total_pages": (total + per_page - 1) // per_page
    })

# ====== Socket.IO 이벤트 (클라이언트 접속 확인용) ======
@socketio.on("connect")
def handle_connect():
    print("Client connected")
    # 연결 시 최신 데이터 전송
    socketio.emit("batteryUpdate", {"gravity": latest_data["sg"], "level": latest_data["samples"]})

@socketio.on("disconnect")
def handle_disconnect():
    print("Client disconnected")

if __name__ == "__main__":
  socketio.run(app, host="0.0.0.0", port=5000)

