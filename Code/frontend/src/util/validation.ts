export function validIpv4(value: string): boolean {
  const parts = value.trim().split('.');
  if (parts.length !== 4) return false;
  return parts.every((part) => {
    if (!/^\d{1,3}$/.test(part)) return false;
    const n = Number(part);
    return n >= 0 && n <= 255;
  });
}

export function validHostnameOrIp(value: string, allowEmpty = false): boolean {
  const s = value.trim();
  if (!s) return allowEmpty;
  if (s.length > 253) return false;
  if (validIpv4(s)) return true;
  return s.split('.').every((label) =>
    label.length >= 1 &&
    label.length <= 63 &&
    /^[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?$/.test(label)
  );
}

export function validDeviceHostname(value: string): boolean {
  const s = value.trim();
  return s.length >= 1 &&
    s.length <= 63 &&
    /^[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?$/.test(s);
}

export function validMqttTopic(value: string): boolean {
  const s = value.trim();
  return s.length >= 1 &&
    s.length <= 128 &&
    !s.startsWith('/') &&
    !s.endsWith('/') &&
    /^[A-Za-z0-9_.\-/]+$/.test(s) &&
    !s.includes('+') &&
    !s.includes('#');
}

export function validNoControl(value: string, maxLen: number, allowEmpty = true): boolean {
  if (!value) return allowEmpty;
  return value.length <= maxLen && !/[\u0000-\u001f\u007f]/.test(value);
}
