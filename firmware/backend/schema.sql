-- schema.sql
-- SQLite database schema for road density dashboard
-- Supports calculation of flow (vehicles/hour) and density (vehicles/km)
-- Includes routes, nodes, vehicles, and history tables with appropriate constraints

-- Routes table to store route metadata
CREATE TABLE IF NOT EXISTS routes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    description TEXT,
    length_km REAL NOT NULL DEFAULT 0.005
);

-- Nodes table to store nodes per route
CREATE TABLE IF NOT EXISTS nodes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    route_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    latitude REAL,
    longitude REAL,
    FOREIGN KEY (route_id) REFERENCES routes(id)
);

-- Vehicles table for real-time vehicle detections
CREATE TABLE IF NOT EXISTS vehicles (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    route_id INTEGER NOT NULL,
    node TEXT NOT NULL,
    distance REAL NOT NULL,
    speed REAL NOT NULL,
    type TEXT NOT NULL,
    timestamp TIMESTAMP NOT NULL,
    FOREIGN KEY (route_id) REFERENCES routes(id),
    UNIQUE (route_id, node, distance, timestamp)
);

-- History table to store archived detection data for flow and density calculations
CREATE TABLE IF NOT EXISTS history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    route_id INTEGER NOT NULL,
    node TEXT NOT NULL,
    distance REAL NOT NULL,
    speed REAL NOT NULL,
    type TEXT NOT NULL,
    timestamp TIMESTAMP NOT NULL,
    count INTEGER NOT NULL DEFAULT 1,
    FOREIGN KEY (route_id) REFERENCES routes(id),
    UNIQUE (route_id, node, distance, timestamp)
);

-- Indexes for performance on frequently queried columns
CREATE INDEX IF NOT EXISTS idx_vehicles_timestamp ON vehicles(timestamp);
CREATE INDEX IF NOT EXISTS idx_vehicles_route_node ON vehicles(route_id, node);
CREATE INDEX IF NOT EXISTS idx_nodes_name ON nodes(name);
CREATE INDEX IF NOT EXISTS idx_history_timestamp ON history(timestamp);
CREATE INDEX IF NOT EXISTS idx_history_route_node ON history(route_id, node);
