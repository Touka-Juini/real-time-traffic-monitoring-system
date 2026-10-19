import json
import paho.mqtt.client as mqtt

# Fonction appelée à chaque fois qu'un message est reçu
def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode('utf-8')  
        data = json.loads(payload)            

        # Extraction des champs du message JSON
        node = data.get('node')               
        count = data.get('count')             
        distance = data.get('distance')       
        timestamp = data.get('timestamp')     

        # Affichage dans la console
        print(f"[{node}] Count: {count}, Distance: {distance}, Timestamp: {timestamp}")  

        # Ici tu pourrais enregistrer en base de données ou envoyer via SocketIO

    except Exception as e:
        print("Erreur lors du traitement du message :", e)  

# Configuration du client MQTT
client = mqtt.Client()                            
client.on_message = on_message                  
client.connect("mqtt_broker_address", 1883)       
client.subscribe("vehicle/counter")               

client.loop_forever()                             

-----------------------------------------------------------
import json
import paho.mqtt.client as mqtt
import requests

# MQTT callback

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode('utf-8')
        data = json.loads(payload)

        requests.post("http://localhost:5000/vehicle", json=data)
    except Exception as e:
        print("Erreur lors du traitement du message :", e)

client = mqtt.Client()
client.on_message = on_message
client.connect("localhost", 1883)
client.subscribe("vehicle/counter")
client.loop_forever()
