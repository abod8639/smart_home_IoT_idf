const functions = require("firebase-functions");
const admin = require("firebase-admin");
admin.initializeApp();

const db = admin.database();

const express = require("express");
const app = express();
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// Mock OAuth Authorization Endpoint
app.get("/auth", (req, res) => {
  const redirectUri = req.query.redirect_uri;
  const state = req.query.state;
  const responseUrl = `${redirectUri}?code=mock_code&state=${state}`;
  return res.redirect(responseUrl);
});

// Mock OAuth Token Endpoint
app.post("/token", (req, res) => {
  res.setHeader("Content-Type", "application/json");
  return res.status(200).json({
    token_type: "bearer",
    access_token: "mock-access-token",
    refresh_token: "mock-refresh-token",
    expires_in: 3600
  });
});

// Smart Home Intent Webhook
app.post("/smarthome", async (req, res) => {
  const body = req.body;
  const intent = body.inputs[0].intent;
  const requestId = body.requestId;

  if (intent === "action.devices.SYNC") {
    return res.json({
      requestId: requestId,
      payload: {
        agentUserId: "esp32_smart_home_user",
        devices: [
          {
            id: "relay_1",
            type: "action.devices.types.SWITCH",
            traits: ["action.devices.traits.OnOff"],
            name: { name: "Relay 1" },
            willReportState: true
          },
          {
            id: "relay_2",
            type: "action.devices.types.SWITCH",
            traits: ["action.devices.traits.OnOff"],
            name: { name: "Relay 2" },
            willReportState: true
          },
          {
            id: "relay_3",
            type: "action.devices.types.SWITCH",
            traits: ["action.devices.traits.OnOff"],
            name: { name: "Relay 3" },
            willReportState: true
          },
          {
            id: "relay_4",
            type: "action.devices.types.SWITCH",
            traits: ["action.devices.traits.OnOff"],
            name: { name: "Relay 4" },
            willReportState: true
          },
          {
            id: "pwm_lamp",
            type: "action.devices.types.LIGHT",
            traits: ["action.devices.traits.OnOff", "action.devices.traits.Brightness"],
            name: { name: "Lamp" },
            willReportState: true
          }
        ]
      }
    });
  }

  if (intent === "action.devices.QUERY") {
    // Read states from Firebase RTDB
    const snapshot = await db.ref("devices/esp32_smart_home_1").once("value");
    const deviceData = snapshot.val() || {};
    const pins = deviceData.pins || {};

    const devices = {
      relay_1: {
        on: pins.relay_1 === 1,
        online: true
      },
      relay_2: {
        on: pins.relay_2 === 1,
        online: true
      },
      relay_3: {
        on: pins.relay_3 === 1,
        online: true
      },
      relay_4: {
        on: pins.relay_4 === 1,
        online: true
      },
      pwm_lamp: {
        on: pins.pwm_lamp > 0,
        brightness: Math.round((pins.pwm_lamp || 0) * 100 / 255),
        online: true
      }
    };

    return res.json({
      requestId: requestId,
      payload: { devices: devices }
    });
  }

  if (intent === "action.devices.EXECUTE") {
    const commands = body.inputs[0].payload.commands;
    const results = [];

    for (const command of commands) {
      const devices = command.devices;
      const executions = command.execution;

      for (const device of devices) {
        const devId = device.id;
        for (const execution of executions) {
          const cmdName = execution.command;
          const params = execution.params;

          let commandObj = null;
          let updateState = {};

          if (devId === "relay_1" && cmdName === "action.devices.commands.OnOff") {
            commandObj = { action: "set_relay", pin: 2, value: params.on ? 1 : 0 };
            updateState = { "pins/relay_1": params.on ? 1 : 0 };
          } else if (devId === "relay_2" && cmdName === "action.devices.commands.OnOff") {
            commandObj = { action: "set_relay", pin: 18, value: params.on ? 1 : 0 };
            updateState = { "pins/relay_2": params.on ? 1 : 0 };
          } else if (devId === "relay_3" && cmdName === "action.devices.commands.OnOff") {
            commandObj = { action: "set_relay", pin: 19, value: params.on ? 1 : 0 };
            updateState = { "pins/relay_3": params.on ? 1 : 0 };
          } else if (devId === "relay_4" && cmdName === "action.devices.commands.OnOff") {
            commandObj = { action: "set_relay", pin: 21, value: params.on ? 1 : 0 };
            updateState = { "pins/relay_4": params.on ? 1 : 0 };
          } else if (devId === "pwm_lamp") {
            if (cmdName === "action.devices.commands.OnOff") {
              const duty = params.on ? 255 : 0;
              commandObj = { action: "set_pwm", pin: 22, value: duty };
              updateState = { "pins/pwm_lamp": duty };
            } else if (cmdName === "action.devices.commands.Brightness") {
              const duty = Math.round(params.brightness * 255 / 100);
              commandObj = { action: "set_pwm", pin: 22, value: duty };
              updateState = { "pins/pwm_lamp": duty };
            }
          }

          if (commandObj) {
            // Write to commands node for ESP32 to poll
            await db.ref("devices/esp32_smart_home_1/commands").set(commandObj);
            // Write directly to pins state in database for immediate feedback
            await db.ref("devices/esp32_smart_home_1").update(updateState);

            results.push({
              ids: [devId],
              status: "SUCCESS",
              states: {
                on: params.on !== undefined ? params.on : (updateState[`pins/${devId}`] > 0),
                online: true
              }
            });
          } else {
            results.push({
              ids: [devId],
              status: "ERROR",
              errorCode: "deviceNotReady"
            });
          }
        }
      }
    }

    return res.json({
      requestId: requestId,
      payload: { commands: results }
    });
  }

  return res.status(400).send("Invalid intent");
});

exports.googleHome = functions.https.onRequest(app);
