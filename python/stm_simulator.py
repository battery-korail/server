# import paho.mqtt.client as mqtt
# import json
# import random
# import time
# import os

# # MQTT 브로커 환경 변수
# MQTT_BROKER = os.environ.get("MQTT_BROKER", "localhost")  
# MQTT_PORT = int(os.environ.get("MQTT_PORT", 1883))
# MQTT_TOPIC = os.environ.get("MQTT_TOPIC", "stm/dp")

# client = mqtt.Client()
# client.connect(MQTT_BROKER, MQTT_PORT, 60)
# client.loop_start()

# try:
#     while True:
#         # 임의로 STM 센서 데이터 생성
#         dp_pa = round(random.uniform(45, 50), 2)  # 차압(Pa)
#         samples = random.randint(5, 15)

#         payload = {
#             "dp_pa": dp_pa,
#             "samples": samples
#         }

#         client.publish(MQTT_TOPIC, json.dumps(payload))
#         print(f"Published: {payload}")

#         time.sleep(5)  
# except KeyboardInterrupt:
#     client.loop_stop()
#     print("Simulator stopped.")
