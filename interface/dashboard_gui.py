import tkinter as tk
from tkinter import Label
import paho.mqtt.client as mqtt
import json

# MQTT Configuration
MQTT_BROKER = "192.168.1.102"
MQTT_TOPIC = "node2/vehicle"

# Initialize GUI
root = tk.Tk()
root.title("Vehicle Detection Dashboard")
root.geometry("400x200")

vehicle_count_var = tk.StringVar(value="Vehicle Count: 0")
distance_var = tk.StringVar(value="Distance: 0 mm")

vehicle_count_label = Label(root, textvariable=vehicle_count_var, font=("Arial", 14))
vehicle_count_label.pack(pady=10)

distance_label = Label(root, textvariable=distance_var, font=("Arial", 14))
distance_label.pack(pady=10)

# MQTT Callbacks
def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT Broker!")
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode('utf-8')
        print("Received MQTT Data:", payload)
        json_data = json.loads(payload)
        vehicle_count_var.set(f"Vehicle Count: {json_data['vehicle_count']}")
        distance_var.set(f"Distance: {json_data['distance']} mm")
    except Exception as e:
        print("Error parsing MQTT message:", e)

mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.connect(MQTT_BROKER, 1883, 60)
mqtt_client.loop_start()

root.mainloop()
