// ================================================
// STREET LIGHT ACCIDENT ALERT SERVER (FULL VERSION)
// ================================================
const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');
const path = require('path');

const app = express();
const PORT = 3000;

// Middleware
app.use(cors());
app.use(bodyParser.json());

// Data storage
let currentStatus = {
    message: "Waiting for data from nodes...",
    nodes: []
};

let alertHistory = [];
let clients = [];   // SSE clients

// =============================================================
// Utility: Transform ESP8266 node into WEB-FRIENDLY structure
// =============================================================
function formatNode(node) {
    return {
        nodeId: node.nodeId,

        connectionStatus: node.connectionStatus || "Unknown",

        gps: {
            latitude: node.latitude || 0,
            longitude: node.longitude || 0,
            altitude: node.altitude || 0,
            satellites: node.satellites || 0
        },

        sensors: {
            ultrasonic: {
                distance: node.distance || 0
            },
            vibration: {
                count: node.vibrationCount || 0
            },
            ldr: {
                lightLevel: node.lightLevel || 0
            }
        },

        eventType: node.eventType || "Unknown",
        ledState: node.ledState || false,
        lastSeen: node.lastSeen || 0
    };
}

// =============================================================
// Utility: Format Alerts for webpage
// =============================================================
function formatAlert(alert) {
    return {
        alertType: alert.alertType,
        severity: alert.severity,
        nodeId: alert.nodeId,

        timestamp: alert.timestamp || Date.now(),
        receivedAt: new Date().toISOString(),

        gps: {
            latitude: alert.location?.latitude || 0,
            longitude: alert.location?.longitude || 0,
            altitude: alert.location?.altitude || 0,
            satellites: alert.location?.satellites || 0,
            mapLink: alert.location?.mapLink || ""
        },

        sensors: {
            distance: alert.sensors?.distance || 0,
            vibrationCount: alert.sensors?.vibrationCount || 0,
            speed: alert.sensors?.speed || 0
        },

        raw: alert
    };
}

// =============================================================
// 1️⃣ STATUS ROUTE (ESP8266 sends every 30 sec)
// =============================================================
app.post('/api/sensor-data', (req, res) => {
    console.log('\n📊 STATUS UPDATE RECEIVED');

    const raw = req.body;
    currentStatus.lastUpdated = new Date().toISOString();

    // Transform node list
    if (raw.nodes) {
        currentStatus.nodes = raw.nodes.map(n => formatNode(n));
    }

    // Broadcast to webpage clients
    broadcastToClients({
        type: "status",
        data: currentStatus
    });

    res.json({
        success: true,
        message: "Status received"
    });

    console.log(`📡 Status updated. Nodes: ${currentStatus.nodes.length}`);
});

// =============================================================
// 2️⃣ ALERT ROUTE (Accident / Object detection)
// =============================================================
app.post('/api/alert', (req, res) => {
    console.log('\n🚨 CRITICAL ALERT RECEIVED!');

    const formatted = formatAlert(req.body);

    alertHistory.unshift(formatted);
    if (alertHistory.length > 100) alertHistory.pop();

    broadcastToClients({
        type: "alert",
        data: formatted
    });

    res.json({
        success: true,
        message: "Alert received and broadcasted",
        alert: formatted
    });
});

// =============================================================
// 3️⃣ GET STATUS (dashboard pull)
// =============================================================
app.get('/api/sensor-data', (req, res) => {
    res.json(currentStatus);
});

// =============================================================
// 4️⃣ GET ALERT HISTORY
// =============================================================
app.get('/api/alerts', (req, res) => {
    res.json({
        total: alertHistory.length,
        alerts: alertHistory
    });
});

// =============================================================
// 5️⃣ SERVER-SENT EVENTS STREAM (REAL-TIME UPDATES)
// =============================================================
app.get('/api/stream', (req, res) => {

    res.setHeader("Content-Type", "text/event-stream");
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader("Connection", "keep-alive");

    clients.push(res);
    console.log(`➕ Web client connected. Total: ${clients.length}`);

    // Immediately send current status
    res.write(`data: ${JSON.stringify({
        type: "status",
        data: currentStatus
    })}\n\n`);

    // On disconnect
    req.on("close", () => {
        clients = clients.filter(c => c !== res);
        console.log(`❌ Web client disconnected. Total: ${clients.length}`);
    });
});

// =============================================================
// BROADCAST FUNCTION — notify webpage clients
// =============================================================
function broadcastToClients(message) {
    const data = `data: ${JSON.stringify(message)}\n\n`;

    clients.forEach(client => {
        client.write(data);
    });

    if (clients.length > 0) {
        console.log(`📤 Broadcasted to ${clients.length} client(s)`);
    }
}

// =============================================================
// 6️⃣ Serve Web Dashboard
// =============================================================
app.get("/", (req, res) => {
    res.sendFile(path.join(__dirname, "WEBPAGE.html"));
});

// =============================================================
// START SERVER
// =============================================================
app.listen(PORT, "0.0.0.0", () => {
    console.log("\n==============================================");
    console.log(" STREET LIGHT ALERT SERVER RUNNING ");
    console.log("==============================================");
    console.log(`🌐 Dashboard:        http://localhost:${PORT}`);
    console.log(`📊 Status API:       http://localhost:${PORT}/api/sensor-data`);
    console.log(`🚨 Alert API:        http://localhost:${PORT}/api/alert`);
    console.log(`📜 Alert History:    http://localhost:${PORT}/api/alerts`);
    console.log("\nWaiting for ESP8266 sensor data...");
});
