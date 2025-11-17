import psycopg2
import os
from dotenv import load_dotenv

load_dotenv()

conn = psycopg2.connect(
    host=os.getenv("PG_HOST", "postgres"),
    port=int(os.getenv("PG_PORT", 5432)),
    dbname=os.getenv("PG_DB", "battery_data"),
    user=os.getenv("POSTGRES_USER", "admin"),
    password=os.getenv("POSTGRES_PASSWORD", "admin")
)

cur = conn.cursor()
cur.execute("SELECT * FROM dp_log ORDER BY id DESC LIMIT 5;")
rows = cur.fetchall()
for r in rows:
    print(r)

cur.close()
conn.close()
