const mqttStateKeys: Record<number, string> = {
  [-4]: 'mqtt.state.timeout',
  [-3]: 'mqtt.state.lost',
  [-2]: 'mqtt.state.cannot',
  [-1]: 'mqtt.state.disconnected',
  0: 'mqtt.state.connected',
  1: 'mqtt.state.badProtocol',
  2: 'mqtt.state.badClientId',
  3: 'mqtt.state.serverUnavailable',
  4: 'mqtt.state.badCredentials',
  5: 'mqtt.state.unauthorized',
};

/**
 * @brief Convert a PubSubClient state code to a locale key.
 * @param state MQTT state code from the firmware.
 * @returns Translation key for the status.
 */
export function mqttStateKey(state: number | undefined): string {
  return state == null ? 'cloud.stateUnknown' : mqttStateKeys[state] || 'cloud.stateUnknown';
}

/**
 * @brief Check whether MQTT is currently connected.
 * @param state MQTT state code from the firmware.
 */
export function isMqttConnected(state: number | undefined): boolean {
  return state === 0;
}

/**
 * @brief Check whether an MQTT state represents an actionable error.
 * @param state MQTT state code from the firmware.
 */
export function isMqttError(state: number | undefined): boolean {
  return state != null && state !== 0 && state !== -1;
}

/**
 * @brief Decide whether the MQTT indicator should be visible.
 * @param state MQTT state code from the firmware.
 */
export function shouldShowMqttIndicator(state: number | undefined): boolean {
  return state != null && state !== -1;
}
