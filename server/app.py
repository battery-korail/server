# from flask import Flask, request, jsonify
# import psycopg2
# from datetime import datetime
# import os

# app = Flask(__name__)



# conn = psycopg2.connect(
#     dbname=DB_NAME,
#     user=DB_USER,
#     password=DB_PASS,
#     host=DB_HOST,
#     port=DB_PORT
# )
# cur = conn.cursor()


# @app.route('/upload', methods=['POST'])
# def upload():
#     data = request.get_json()
#     voltage = data.get('voltage')
#     current = data.get('current')
#     temperature = data.get('temperature')
#     timestamp = datetime.now()

#     cur.execute(
#         "INSERT INTO battery_logs (timestamp, voltage, current, temperature) VALUES (%s, %s, %s, %s)",
#         (timestamp, voltage, current, temperature)
#     )
#     conn.commit()

#     return jsonify({"status": "ok"})

# @app.route('/')
# def index():
#     return "ESP data receiver is running!"

# if __name__ == '__main__':
#     app.run(host='0.0.0.0', port=5000)
