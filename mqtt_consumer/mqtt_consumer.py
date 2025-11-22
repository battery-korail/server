import os, json, psycopg2, paho.mqtt.client as mqtt
from dotenv import load_dotenv
load_dotenv()
MQTT_BROKER = os.getenv("MQTT_BROKER")
MQTT_PORT   = int(os.getenv("MQTT_PORT", 1883))  # 포트 기본값은 OK
MQTT_TOPIC  = os.getenv("MQTT_TOPIC")

# PostgreSQL 관련
PG_HOST = os.getenv("PG_HOST")
PG_PORT = int(os.getenv("PG_PORT", 5432))
PG_DB   = os.getenv("PG_DB")
PG_USER = os.getenv("PG_USER")
PG_PASS = os.getenv("PG_PASS")

# 연결 확인
for var_name, var_value in [("MQTT_BROKER", MQTT_BROKER), ("MQTT_TOPIC", MQTT_TOPIC),
                            ("PG_HOST", PG_HOST), ("PG_DB", PG_DB), ("PG_USER", PG_USER), ("PG_PASS", PG_PASS)]:
    if not var_value:
        raise EnvironmentError(f"{var_name} environment variable is not set!")

def get_conn():
    return psycopg2.connect(
        host=PG_HOST,
        port=PG_PORT,
        dbname=PG_DB,
        user=PG_USER,
        password=PG_PASS
    )


def ensure_table():
    conn = get_conn()
    cur = conn.cursor()
    cur.execute("""
        CREATE TABLE IF NOT EXISTS dp_log (
            id SERIAL PRIMARY KEY,
            read_time TIMESTAMP WITH TIME ZONE DEFAULT now(),
            sg DOUBLE PRECISION,
            samples INTEGER
        );
    """)
    conn.commit()
    cur.close()
    conn.close()

ensure_table()

def on_connect(client, userdata, flags, rc):
    print("[MQTT] Connected with code", rc)
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode()
        data = json.loads(payload)
        sg = float(data.get("sg", 0))
        samples = int(data.get("samples", 0))
        print(f"[MQTT] sg={sg}, samples={samples}")

        conn = get_conn()
        cur = conn.cursor()
        cur.execute("INSERT INTO dp_log (sg, samples) VALUES (%s, %s)", (sg, samples))
        conn.commit()
        cur.close()
        conn.close()
        print("[DB] Inserted successfully")
    except Exception as e:
        print("[Error] Processing message:", e)

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop_forever()
