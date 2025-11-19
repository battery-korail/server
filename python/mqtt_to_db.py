# import paho.mqtt.client as mqtt
# import psycopg2
# import random
# import time


    
# )
# cur = conn.cursor()

# # MQTT 콜백
# def on_connect(client, userdata, flags, rc):
#     print("Connected with result code", rc)

# client = mqtt.Client()
# client.on_connect = on_connect
# client.connect("localhost", 1883, 60)
# client.loop_start()

# while True:
#     voltage = round(random.uniform(11.5, 12.6), 2)
#     current = round(random.uniform(0.5, 2.0), 2)
#     temp = round(random.uniform(25.0, 40.0), 1)
    
#     # MQTT Publish
#     client.publish("battery/data", f"{voltage},{current},{temp}")
#     print(f"Published: {voltage},{current},{temp}")
#     # DB Insert
#     cur.execute(
#         "INSERT INTO battery_logs (voltage, current, temperature) VALUES (%s, %s, %s)",
#         (voltage, current, temp)
#     )
#     conn.commit()
#     print(f"Inserted into DB: {voltage},{current},{temp}")
#     time.sleep(5)
