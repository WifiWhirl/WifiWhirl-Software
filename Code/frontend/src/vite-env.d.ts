/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** "1" on esp32 builds (set by build_frontend.py) to expose the MSPA hardware option. */
  readonly VITE_ENABLE_MSPA?: string;
}
