import { useEffect, useState } from "react";
import { cmd } from "@/commands";
import type { VideoDeviceInfo } from "@/types/config";

/** The system's video devices, listed once on mount. Empty on failure. */
export function useVideoDevices(): VideoDeviceInfo[] {
  const [videoDevices, setVideoDevices] = useState<VideoDeviceInfo[]>([]);

  useEffect(() => {
    const fetchDevices = async () => {
      try {
        setVideoDevices(await cmd.face.listVideoDevices());
      } catch (err) {
        console.error("Failed to fetch devices:", err);
      }
    };

    fetchDevices();
  }, []);

  return videoDevices;
}
