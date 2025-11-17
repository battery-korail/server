import os
import json
import psycopg2
import paho.mqtt.client as mqtt
from dotenv import load_dotenv

# .env 로드
load_dotenv()

# ----- 환경 변수 -----
MQTT_BROKER = os.getenv("MQTT_BROKER", "mqtt")
MQTT_PORT   = int(os.getenv("MQTT_PORT", 1883))
MQTT_TOPIC  = os.getenv("MQTT_TOPIC", "stm/dp")

PG_HOST = os.getenv("PG_HOST", "postgres")
PG_PORT = int(os.getenv("PG_PORT", 5432))
PG_DB   = os.getenv("PG_DB", "battery_data")
PG_USER = os.getenv("POSTGRES_USER", "postgres")
PG_PASS = os.getenv("POSTGRES_PASSWORD", "your_password")

# ----- PostgreSQL 연결 함수 -----
def get_conn():
    return psycopg2.connect(
        host=PG_HOST,
        port=PG_PORT,
        dbname=PG_DB,
        user=PG_USER,
        password=PG_PASS
    )

# ----- 테이블 확인 및 생성 -----
def ensure_table():
    conn = get_conn()
    cur = conn.cursor()
    cur.execute("""
    CREATE TABLE IF NOT EXISTS dp_log (
        id SERIAL PRIMARY KEY,
        read_time TIMESTAMP WITH TIME ZONE DEFAULT now(),
        dp_pa DOUBLE PRECISION,
        samples INTEGER
    );
    """)
    conn.commit()
    cur.close()
    conn.close()

ensure_table()

# ----- MQTT 콜백 -----
def on_connect(client, userdata, flags, rc, properties=None):
    print(f"[MQTT] Connected with result code {rc}")
    client.subscribe(MQTT_TOPIC)
    print(f"[MQTT] Subscribed to topic '{MQTT_TOPIC}'")

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode()
        obj = json.loads(payload)
        dp = float(obj.get("dp_pa"))
        samples = int(obj.get("samples", 0))
        print(f"[MQTT] source={source}, dp={dp:.2f}, samples={samples}")

        # DB에 삽입
        conn = get_conn()
        cur = conn.cursor()
        cur.execute(
            "INSERT INTO dp_log (dp_pa, samples) VALUES (%s, %s)",
            (dp, samples)
        )
        conn.commit()

        # 확인용 출력
        cur.execute("SELECT * FROM dp_log ORDER BY id DESC LIMIT 5;")
        rows = cur.fetchall()
        for r in rows:
            print("[DB CHECK]", r)

        cur.close()
        conn.close()

    except Exception as e:
        print("[Error] Processing message:", e)


# ----- MQTT 클라이언트 시작 -----
client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

print(f"[MQTT] Connecting to broker {MQTT_BROKER}:{MQTT_PORT} ...")
client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop_forever()



# #시뮬레이터 

# import os
# import json
# import psycopg2
# import paho.mqtt.client as mqtt

# # ----- 환경 변수 -----
# MQTT_BROKER = os.environ.get("MQTT_BROKER", "mqtt")
# MQTT_PORT = int(os.environ.get("MQTT_PORT", 1883))
# MQTT_TOPIC = os.environ.get("MQTT_TOPIC", "stm/dp")

# PG_HOST = os.environ.get("PG_HOST", "postgres")
# PG_PORT = int(os.environ.get("PG_PORT", 5432))
# PG_DB = os.environ.get("PG_DB", "battery_data")
# PG_USER = os.environ.get("PG_USER", "postgres")
# PG_PASS = os.environ.get("PG_PASS", "your_password")

# # ----- PostgreSQL 연결 함수 -----
# def get_conn():
#     return psycopg2.connect(
#         host=PG_HOST,
#         port=PG_PORT,
#         dbname=PG_DB,
#         user=PG_USER,
#         password=PG_PASS
#     )

# # ----- 테이블 확인 및 생성 -----
# def ensure_table():
#     conn = get_conn()
#     cur = conn.cursor()
#     cur.execute("""
#     CREATE TABLE IF NOT EXISTS dp_log (
#         id SERIAL PRIMARY KEY,
#         read_time TIMESTAMP WITH TIME ZONE DEFAULT now(),
#         dp_pa DOUBLE PRECISION,
#         samples INTEGER
#     );
#     """)
#     conn.commit()
#     cur.close()
#     conn.close()

# ensure_table()

# # ----- MQTT 콜백 -----
# def on_connect(client, userdata, flags, rc, properties=None):
#     print(f"[MQTT] Connected with result code {rc}")
#     client.subscribe(MQTT_TOPIC)
#     print(f"[MQTT] Subscribed to topic '{MQTT_TOPIC}'")

# def on_message(client, userdata, msg):
#     try:
#         payload = msg.payload.decode()
#         obj = json.loads(payload)
#         dp = float(obj.get("dp_pa"))
#         samples = int(obj.get("samples", 0))

#         # DB에 삽입
#         conn = get_conn()
#         cur = conn.cursor()
#         cur.execute(
#             "INSERT INTO dp_log (dp_pa, samples) VALUES (%s, %s)",
#             (dp, samples)
#         )
#         conn.commit()
#         cur.close()
#         conn.close()

#         print(f"[DB] Inserted dp={dp:.2f} Pa, samples={samples}")
#     except Exception as e:
#         print("[Error] Processing message:", e)

# # ----- MQTT 클라이언트 시작 -----
# client = mqtt.Client()
# client.on_connect = on_connect
# client.on_message = on_message

# print(f"[MQTT] Connecting to broker {MQTT_BROKER}:{MQTT_PORT} ...")
# client.connect(MQTT_BROKER, MQTT_PORT, 60)
# client.loop_forever()
