FROM python:3.10-slim
WORKDIR /app
COPY . .
RUN pip install flask psycopg2-binary
CMD ["python", "app.py"]
