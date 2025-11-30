// ======================= BACKEND SERVER - EMERGENCY ALERT SYSTEM =======================
// Stack: Node.js + Express + MongoDB + Twilio/Email
// Purpose: Receive accident data from Gateway Hub and dispatch emergency alerts

const express = require('express');
const mongoose = require('mongoose');
const nodemailer = require('nodemailer');
const twilio = require('twilio');
const cors = require('cors');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3000;

// ======================= MIDDLEWARE =======================
app.use(cors());
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// Request Logging
app.use((req, res, next) => {
  console.log(`[${new Date().toISOString()}] ${req.method} ${req.path}`);
  next();
});

// ======================= MONGODB CONNECTION =======================
mongoose.connect(process.env.MONGODB_URI || 'mongodb://localhost:27017/streetlight_emergency', {
  useNewUrlParser: true,
  useUnifiedTopology: true
}).then(() => {
  console.log('✓ MongoDB Connected');
}).catch(err => {
  console.error('❌ MongoDB Connection Error:', err);
});

// ======================= DATABASE SCHEMAS =======================

// Accident Event Schema
const AccidentSchema = new mongoose.Schema({
  node_id: { type: Number, required: true },
  event_type: { type: String, default: 'ACCIDENT' },
  latitude: { type: Number, required: true },
  longitude: { type: Number, required: true },
  timestamp: { type: Number, required: true },
  gateway_id: { type: Number, required: true },
  reported_at: { type: Number, required: true },
  wifi_rssi: { type: Number },
  gateway_ip: { type: String },
  
  // Alert Status
  alert_sent: { type: Boolean, default: false },
  alert_sent_at: { type: Date },
  emergency_services_notified: [String], // ['police', 'ambulance', 'fire']
  
  // Location Details
  address: { type: String },
  nearest_hospital: { type: String },
  nearest_police_station: { type: String },
  
  // Response Tracking
  response_status: { 
    type: String, 
    enum: ['pending', 'dispatched', 'on-scene', 'resolved'], 
    default: 'pending' 
  },
  responders: [String],
  resolution_notes: { type: String },
  resolved_at: { type: Date },
  
  created_at: { type: Date, default: Date.now }
});

const Accident = mongoose.model('Accident', AccidentSchema);

// Traffic Analytics Schema
const TrafficSchema = new mongoose.Schema({
  node_id: { type: Number, required: true },
  timestamp: { type: Number, required: true },
  created_at: { type: Date, default: Date.now }
});

const Traffic = mongoose.model('Traffic', TrafficSchema);

// Emergency Contacts Schema
const EmergencyContactSchema = new mongoose.Schema({
  name: { type: String, required: true },
  phone: { type: String, required: true },
  email: { type: String },
  role: { type: String, enum: ['police', 'ambulance', 'fire', 'admin'], required: true },
  location: { type: String },
  active: { type: Boolean, default: true }
});

const EmergencyContact = mongoose.model('EmergencyContact', EmergencyContactSchema);

// ======================= TWILIO SETUP (SMS) =======================
const twilioClient = twilio(
  process.env.TWILIO_ACCOUNT_SID,
  process.env.TWILIO_AUTH_TOKEN
);
const TWILIO_PHONE = process.env.TWILIO_PHONE_NUMBER;

// ======================= NODEMAILER SETUP (EMAIL) =======================
const emailTransporter = nodemailer.createTransport({
  service: 'gmail', // or 'smtp.office365.com' for Outlook
  auth: {
    user: process.env.EMAIL_USER,
    pass: process.env.EMAIL_PASSWORD
  }
});

// ======================= API ROUTES =======================

// Health Check
app.get('/', (req, res) => {
  res.json({ 
    status: 'online',
    service: 'Street Light Emergency Alert System',
    version: '1.0.0',
    timestamp: new Date().toISOString()
  });
});

// ======================= ACCIDENT REPORTING ENDPOINT =======================
app.post('/api/accident', async (req, res) => {
  try {
    console.log('\n🚨 ===== ACCIDENT ALERT RECEIVED ===== 🚨');
    console.log('Payload:', JSON.stringify(req.body, null, 2));

    const {
      node_id,
      event_type,
      latitude,
      longitude,
      timestamp,
      gateway_id,
      reported_at,
      wifi_rssi,
      gateway_ip
    } = req.body;

    // Validate required fields
    if (!node_id || !latitude || !longitude) {
      return res.status(400).json({ 
        error: 'Missing required fields',
        required: ['node_id', 'latitude', 'longitude']
      });
    }

    // Check for duplicate (within last 2 minutes)
    const twoMinutesAgo = Date.now() - 120000;
    const duplicate = await Accident.findOne({
      node_id: node_id,
      latitude: latitude,
      longitude: longitude,
      created_at: { $gte: new Date(twoMinutesAgo) }
    });

    if (duplicate) {
      console.log('⚠️  Duplicate accident report detected - Ignoring');
      return res.status(200).json({ 
        message: 'Duplicate report - Already processed',
        accident_id: duplicate._id
      });
    }

    // Get reverse geocoding (address from coordinates)
    const locationDetails = await reverseGeocode(latitude, longitude);

    // Create accident record
    const accident = new Accident({
      node_id,
      event_type: event_type || 'ACCIDENT',
      latitude,
      longitude,
      timestamp,
      gateway_id,
      reported_at,
      wifi_rssi,
      gateway_ip,
      address: locationDetails.address,
      nearest_hospital: locationDetails.nearest_hospital,
      nearest_police_station: locationDetails.nearest_police_station
    });

    await accident.save();
    console.log(`✓ Accident saved to database (ID: ${accident._id})`);

    // Dispatch Emergency Alerts
    const alertResults = await dispatchEmergencyAlerts(accident);

    // Update accident record with alert status
    accident.alert_sent = true;
    accident.alert_sent_at = new Date();
    accident.emergency_services_notified = alertResults.notified;
    await accident.save();

    console.log('✅ Emergency alerts dispatched successfully\n');

    res.status(201).json({
      message: 'Accident report received and emergency services notified',
      accident_id: accident._id,
      location: locationDetails.address,
      alerts_sent: alertResults
    });

  } catch (error) {
    console.error('❌ Error processing accident:', error);
    res.status(500).json({ 
      error: 'Internal server error',
      message: error.message 
    });
  }
});

// ======================= EMERGENCY ALERT DISPATCHER =======================
async function dispatchEmergencyAlerts(accident) {
  const results = {
    sms_sent: [],
    emails_sent: [],
    notified: [],
    errors: []
  };

  try {
    // Get all active emergency contacts
    const contacts = await EmergencyContact.find({ active: true });

    if (contacts.length === 0) {
      console.log('⚠️  No emergency contacts configured');
      return results;
    }

    // Prepare alert message
    const googleMapsLink = `https://www.google.com/maps?q=${accident.latitude},${accident.longitude}`;
    const alertMessage = `
🚨 EMERGENCY ALERT - TRAFFIC ACCIDENT DETECTED 🚨

Location: ${accident.address || 'Address unavailable'}
Coordinates: ${accident.latitude.toFixed(6)}, ${accident.longitude.toFixed(6)}
Node ID: ${accident.node_id}
Time: ${new Date().toLocaleString()}

Map: ${googleMapsLink}

Nearest Hospital: ${accident.nearest_hospital || 'Unknown'}
Nearest Police: ${accident.nearest_police_station || 'Unknown'}

⚠️ IMMEDIATE RESPONSE REQUIRED ⚠️
    `.trim();

    // Send SMS to all contacts
    for (const contact of contacts) {
      // Send SMS
      if (contact.phone && TWILIO_PHONE) {
        try {
          await twilioClient.messages.create({
            body: alertMessage,
            from: TWILIO_PHONE,
            to: contact.phone
          });
          results.sms_sent.push(contact.name);
          results.notified.push(contact.role);
          console.log(`✓ SMS sent to ${contact.name} (${contact.role})`);
        } catch (err) {
          console.error(`❌ SMS failed for ${contact.name}:`, err.message);
          results.errors.push(`SMS to ${contact.name}: ${err.message}`);
        }
      }

      // Send Email
      if (contact.email) {
        try {
          await emailTransporter.sendMail({
            from: process.env.EMAIL_USER,
            to: contact.email,
            subject: '🚨 EMERGENCY: Traffic Accident Detected',
            html: `
              <div style="font-family: Arial, sans-serif; padding: 20px; background-color: #fff3cd; border: 2px solid #ff0000;">
                <h2 style="color: #d32f2f;">🚨 EMERGENCY ALERT 🚨</h2>
                <h3>Traffic Accident Detected</h3>
                
                <table style="width: 100%; border-collapse: collapse; margin: 20px 0;">
                  <tr>
                    <td style="padding: 8px; border: 1px solid #ddd;"><strong>Location:</strong></td>
                    <td style="padding: 8px; border: 1px solid #ddd;">${accident.address || 'Address unavailable'}</td>
                  </tr>
                  <tr>
                    <td style="padding: 8px; border: 1px solid #ddd;"><strong>Coordinates:</strong></td>
                    <td style="padding: 8px; border: 1px solid #ddd;">${accident.latitude.toFixed(6)}, ${accident.longitude.toFixed(6)}</td>
                  </tr>
                  <tr>
                    <td style="padding: 8px; border: 1px solid #ddd;"><strong>Node ID:</strong></td>
                    <td style="padding: 8px; border: 1px solid #ddd;">${accident.node_id}</td>
                  </tr>
                  <tr>
                    <td style="padding: 8px; border: 1px solid #ddd;"><strong>Time:</strong></td>
                    <td style="padding: 8px; border: 1px solid #ddd;">${new Date().toLocaleString()}</td>
                  </tr>
                  <tr>
                    <td style="padding: 8px; border: 1px solid #ddd;"><strong>Nearest Hospital:</strong></td>
                    <td style="padding: 8px; border: 1px solid #ddd;">${accident.nearest_hospital || 'Unknown'}</td>
                  </tr>
                  <tr>
                    <td style="padding: 8px; border: 1px solid #ddd;"><strong>Nearest Police:</strong></td>
                    <td style="padding: 8px; border: 1px solid #ddd;">${accident.nearest_police_station || 'Unknown'}</td>
                  </tr>
                </table>
                
                <a href="${googleMapsLink}" style="display: inline-block; padding: 15px 30px; background-color: #d32f2f; color: white; text-decoration: none; border-radius: 5px; margin: 10px 0;">
                  📍 View Location on Google Maps
                </a>
                
                <p style="color: #d32f2f; font-weight: bold; margin-top: 20px;">
                  ⚠️ IMMEDIATE RESPONSE REQUIRED ⚠️
                </p>
              </div>
            `
          });
          results.emails_sent.push(contact.name);
          console.log(`✓ Email sent to ${contact.name}`);
        } catch (err) {
          console.error(`❌ Email failed for ${contact.name}:`, err.message);
          results.errors.push(`Email to ${contact.name}: ${err.message}`);
        }
      }

      // Small delay to avoid rate limiting
      await new Promise(resolve => setTimeout(resolve, 500));
    }

  } catch (error) {
    console.error('❌ Error in alert dispatcher:', error);
    results.errors.push(error.message);
  }

  return results;
}

// ======================= REVERSE GEOCODING =======================
async function reverseGeocode(lat, lng) {
  try {
    // Using OpenStreetMap Nominatim (Free, no API key needed)
    const fetch = (await import('node-fetch')).default;
    const url = `https://nominatim.openstreetmap.org/reverse?format=json&lat=${lat}&lon=${lng}&zoom=18&addressdetails=1`;
    
    const response = await fetch(url, {
      headers: {
        'User-Agent': 'StreetLightEmergencySystem/1.0'
      }
    });
    
    const data = await response.json();
    
    const address = data.display_name || `${lat.toFixed(6)}, ${lng.toFixed(6)}`;
    
    // Search nearby hospitals and police stations
    const nearbyPlaces = await searchNearbyPlaces(lat, lng);
    
    return {
      address: address,
      nearest_hospital: nearbyPlaces.hospital,
      nearest_police_station: nearbyPlaces.police
    };
  } catch (error) {
    console.error('Geocoding error:', error);
    return {
      address: `${lat.toFixed(6)}, ${lng.toFixed(6)}`,
      nearest_hospital: 'Search manually',
      nearest_police_station: 'Search manually'
    };
  }
}

async function searchNearbyPlaces(lat, lng) {
  try {
    const fetch = (await import('node-fetch')).default;
    
    // Search for hospitals
    const hospitalUrl = `https://nominatim.openstreetmap.org/search?format=json&q=hospital&lat=${lat}&lon=${lng}&limit=1`;
    const hospitalRes = await fetch(hospitalUrl, {
      headers: { 'User-Agent': 'StreetLightEmergencySystem/1.0' }
    });
    const hospitalData = await hospitalRes.json();
    
    // Search for police stations
    const policeUrl = `https://nominatim.openstreetmap.org/search?format=json&q=police&lat=${lat}&lon=${lng}&limit=1`;
    const policeRes = await fetch(policeUrl, {
      headers: { 'User-Agent': 'StreetLightEmergencySystem/1.0' }
    });
    const policeData = await policeRes.json();
    
    return {
      hospital: hospitalData[0]?.display_name || 'Search manually',
      police: policeData[0]?.display_name || 'Search manually'
    };
  } catch (error) {
    return {
      hospital: 'Search manually',
      police: 'Search manually'
    };
  }
}

// ======================= ADDITIONAL API ENDPOINTS =======================

// Get all accidents
app.get('/api/accidents', async (req, res) => {
  try {
    const accidents = await Accident.find().sort({ created_at: -1 }).limit(100);
    res.json({
      count: accidents.length,
      accidents: accidents
    });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Get specific accident
app.get('/api/accidents/:id', async (req, res) => {
  try {
    const accident = await Accident.findById(req.params.id);
    if (!accident) {
      return res.status(404).json({ error: 'Accident not found' });
    }
    res.json(accident);
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Update accident response status
app.patch('/api/accidents/:id/status', async (req, res) => {
  try {
    const { response_status, responders, resolution_notes } = req.body;
    
    const accident = await Accident.findById(req.params.id);
    if (!accident) {
      return res.status(404).json({ error: 'Accident not found' });
    }
    
    accident.response_status = response_status || accident.response_status;
    if (responders) accident.responders = responders;
    if (resolution_notes) accident.resolution_notes = resolution_notes;
    if (response_status === 'resolved') accident.resolved_at = new Date();
    
    await accident.save();
    res.json(accident);
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Add emergency contact
app.post('/api/contacts', async (req, res) => {
  try {
    const contact = new EmergencyContact(req.body);
    await contact.save();
    res.status(201).json(contact);
  } catch (error) {
    res.status(400).json({ error: error.message });
  }
});

// Get all emergency contacts
app.get('/api/contacts', async (req, res) => {
  try {
    const contacts = await EmergencyContact.find();
    res.json(contacts);
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Traffic analytics
app.post('/api/traffic', async (req, res) => {
  try {
    const traffic = new Traffic(req.body);
    await traffic.save();
    res.status(201).json({ message: 'Traffic event logged' });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// Get traffic statistics
app.get('/api/traffic/stats', async (req, res) => {
  try {
    const total = await Traffic.countDocuments();
    const today = await Traffic.countDocuments({
      created_at: { $gte: new Date(new Date().setHours(0, 0, 0, 0)) }
    });
    
    res.json({
      total_traffic_events: total,
      today: today
    });
  } catch (error) {
    res.status(500).json({ error: error.message });
  }
});

// ======================= SERVER START =======================
app.listen(PORT, () => {
  console.log('\n╔════════════════════════════════════════════════╗');
  console.log('║  🚨 EMERGENCY ALERT SERVER RUNNING 🚨          ║');
  console.log('╠════════════════════════════════════════════════╣');
  console.log(`║  Port: ${PORT}                                    ║`);
  console.log(`║  Environment: ${process.env.NODE_ENV || 'development'}                      ║`);
  console.log('╚════════════════════════════════════════════════╝\n');
  console.log('📡 Listening for accident reports from Gateway Hub...\n');
});
