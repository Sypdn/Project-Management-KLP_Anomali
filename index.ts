import mqtt from "mqtt";
import axios from "axios";

import config from "./edge/config.js";
import rules from "./edge/rules.js";


const client = mqtt.connect(config.mqttBroker, {
  username: config.mqttUsername,
  password: config.mqttPassword,
});


client.on("connect", () => {

  console.log("Edge Connected ke broker MQTT:", config.mqttBroker);

  client.subscribe(config.topics.level);
  console.log(`Subscribe topic telemetry: ${config.topics.level}`);

  console.log(
    `Untuk override manual pompa, publish ke topic "${config.topics.pompaCmd}" dengan payload: ON / OFF / AUTO`
  );

});


client.on("message", async (topic, message) => {

  if (topic !== config.topics.level) return;

  let data;
  try {
    data = JSON.parse(message.toString());
  } catch (err) {
    console.error("Payload bukan JSON valid:", message.toString());
    return;
  }

  console.log("Receive:", data);


  // ======================
  // ANALYZE
  // ======================

  const result = rules(data);
  console.log("Status tandon:", result.status);


  // ======================
  // SEND TO CLOUD (ThingsBoard)
  // ======================

  const url = `${config.thingsboard.url}${config.thingsboard.token}/telemetry`;

  try {

    await axios.post(url, {
      distance_cm: data.distance_cm,
      level_cm: data.level_cm,
      level_percent: data.level_percent,
      pump_status: data.pump_status,
      mode: data.mode,
      status: result.status,
    });

    console.log("Terkirim ke ThingsBoard");

  } catch (err: any) {

    console.error("Gagal kirim ke ThingsBoard:", err.message);

  }

});


client.on("error", (err) => {
  console.error("MQTT error:", err.message);
});


console.log("Edge app Kontrol Tandon Air berjalan...");
