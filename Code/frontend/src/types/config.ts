/**
 * @brief Wi-Fi configuration payload returned by /getwifi/.
 */
export interface WifiCfg {
  enableAp?: boolean;
  apSsid?: string;
  enableWM?: boolean;
  enableStaticIp4?: boolean;
  ip4Address?: string;
  ip4Subnet?: string;
  ip4Gateway?: string;
  ip4DnsPrimary?: string;
  ip4DnsSecondary?: string;
  ip4NTP?: string;
}

/**
 * @brief One network returned by /scanwifi/.
 */
export interface WifiNetwork {
  ssid: string;
  rssi: number;
  enc?: boolean | number;
}

/**
 * @brief Wi-Fi scan payload returned by /scanwifi/.
 */
export interface WifiScanResult {
  networks?: WifiNetwork[];
}

/**
 * @brief MQTT configuration payload returned by /getmqtt/.
 */
export interface MqttCfg {
  enableMqtt?: boolean;
  mqttServer?: string;
  mqttPort?: number;
  mqttUsername?: string;
  mqttClientId?: string;
  mqttBaseTopic?: string;
  mqttTelemetryInterval?: number;
}

export interface CloudCfg {
  enabled?: boolean;
  host?: string;
  port?: number;
  webUrl?: string;  // cloud web app base; the Connect button redirects here to pair
  state?: string;   // link state name (read-only): disconnected/connecting/handshaking/connected/disabled
  hasPsk?: boolean; // whether a PSK is provisioned (read-only)
}
