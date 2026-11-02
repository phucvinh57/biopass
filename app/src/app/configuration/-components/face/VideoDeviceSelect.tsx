import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import type { VideoDeviceInfo } from "@/types/config";
import { DISABLED_OPTION, devicePathFor, selectValueFor } from "./selectValue";

const UNAVAILABLE_DEVICE_OPTION = "__unavailable_device__";

interface Props {
  id?: string;
  /** Saved path; null = the "off" option (auto-select or disabled). */
  value: string | null | undefined;
  onChange: (value: string | null) => void;
  videoDevices: VideoDeviceInfo[];
  offLabel: string;
  unavailableLabel: string;
  placeholder?: string;
  disabled?: boolean;
}

/** A /dev/video* picker that saves the device's stable path. */
export function VideoDeviceSelect({
  id,
  value,
  onChange,
  videoDevices,
  offLabel,
  unavailableLabel,
  placeholder,
  disabled,
}: Props) {
  const selectValue = selectValueFor(
    devicePathFor(value || null, videoDevices),
    videoDevices.map((device) => device.path),
    UNAVAILABLE_DEVICE_OPTION,
  );

  return (
    <Select
      value={selectValue}
      disabled={disabled}
      onValueChange={(next) => onChange(next === DISABLED_OPTION ? null : next)}
    >
      <SelectTrigger id={id} className="h-10 w-full">
        <SelectValue placeholder={placeholder ?? offLabel} />
      </SelectTrigger>
      <SelectContent>
        <SelectItem value={DISABLED_OPTION}>{offLabel}</SelectItem>
        {selectValue === UNAVAILABLE_DEVICE_OPTION && (
          <SelectItem value={UNAVAILABLE_DEVICE_OPTION} disabled>
            {unavailableLabel}
          </SelectItem>
        )}
        {videoDevices.length > 0 ? (
          videoDevices.map((device) => (
            <SelectItem key={device.path} value={device.path}>
              {device.display_name}
            </SelectItem>
          ))
        ) : (
          <SelectItem value="__no_devices__" disabled>
            No video devices found
          </SelectItem>
        )}
      </SelectContent>
    </Select>
  );
}
