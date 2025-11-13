FROM python:3.10-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY mqtt_consumer.py .
CMD ["python", "mqtt_consumer.py"]
