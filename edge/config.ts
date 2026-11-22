// Ganti dengan IP broker Mosquitto Anda (PC/Laptop/Raspberry Pi di jaringan lokal)
// Contoh: "mqtt://192.168.1.10:1883"
const mqttBroker = "mqtt://10.87.32.237:1883";

// Karena mosquitto.conf Anda pakai allow_anonymous false + password_file,
// isi username & password yang sudah dibuat dengan mosquitto_passwd
const mqttUsername = "sypdn";
const mqttPassword = "180905";

const thingsboard = {
  token: "A4fiaxvTtXyKBBnmh6Xg",
  url: "https://eu.thingsboard.cloud/api/v1/",
};

const topics = {
  level: "tandon/level",       // ESP32 publish telemetry (JSON) ke sini
  pompaCmd: "tandon/pompa/cmd", // Publish "ON" / "OFF" / "AUTO" ke sini untuk override manual
};


export default {
  mqttBroker,
  mqttUsername,
  mqttPassword,
  thingsboard,
  topics,
};