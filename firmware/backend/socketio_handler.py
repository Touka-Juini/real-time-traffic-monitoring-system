from flask_socketio import SocketIO

socketio = SocketIO(cors_allowed_origins=['http://localhost:5000', 'http://127.0.0.1:5000'])

def emit_vehicle_data(vehicle_data):
    print(f"Emitting new_vehicle: {vehicle_data}")  # Add this
    socketio.emit('new_vehicle', vehicle_data)

def emit_counts(counts):
    print(f"Emitting count_update: {counts}")  # Add this
    socketio.emit('count_update', counts)

def emit_chart_data(data):
    print(f" [Backend] Emitting chart_update: {data}")
    socketio.emit('chart_update', data)
