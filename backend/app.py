from flask import Flask, request, jsonify
from flask_cors import CORS
import psycopg2
from psycopg2.extras import RealDictCursor
import os
from dotenv import load_dotenv

load_dotenv()
app = Flask(__name__)
CORS(app, resources={r"/*": {"origins": "*"}})

# CORS(app, resources={r"/*": {"origins": "*"}})



def get_conn():
    return psycopg2.connect(
        host=os.getenv("PG_HOST"),
        port=int(os.getenv("PG_PORT")),
        dbname=os.getenv("POSTGRES_DB"),
        user=os.getenv("POSTGRES_USER"),
        password=os.getenv("POSTGRES_PASSWORD")
    )


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


# 저장된 값 저장
@app.route("/dp/save", methods=["POST"])
def save_dp():
    try:
        data = request.get_json()
        if not data or "dp_pa" not in data:
            return jsonify({"error": "dp_pa is required"}), 400

        dp_pa = float(data["dp_pa"])

        conn = get_conn()  # DB 연결
        cur = conn.cursor()
        cur.execute(
            "INSERT INTO dp_saved (sg) VALUES (%s) RETURNING id, sg AS dp_pa, created_at;",
            (dp_pa,)
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


# 저장된 값 조회 (페이징)
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

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
