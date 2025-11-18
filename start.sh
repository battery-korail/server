#!/bin/bash
cd ~/iot-stack-old
docker compose up -d
cd backend
source venv/bin/activate
nohup python app.py &
cd ../frontend
yarn dev
