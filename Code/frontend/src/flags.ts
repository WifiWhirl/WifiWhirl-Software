/**
 * @brief Build-time switches for features that ship disabled.
 *
 * Automated heat-loss calibration does not measure reliably yet, so its UI stays
 * hidden for the release. Nothing was removed: the firmware endpoints
 * (/starthlcal/, /cancelhlcal/, /gethlcal/) and the frontend code behind this
 * flag are intact. Manually setting the heat-loss coefficient stays available.
 */
export const SHOW_HEAT_LOSS_CALIBRATION = false;
