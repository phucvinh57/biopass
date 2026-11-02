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
  /** Saved path; null = the "off" option, or unset without one. */
  value: string | null | undefined;
  onChange: (value: string | null) => void;
  videoDevices: VideoDeviceInfo[];
  /** Label of the option that saves null; omit to offer devices only. */
  offLabel?: string;
  unavailableLabel: string;
  placeholder?: string;
  disabled?: boolean;
  /** With a null value: nothing chosen yet, so show the placeholder. */
  unset?: boolean;
}

/** A camera picker that shows device info and saves its stable path. */
export function VideoDeviceSelect({
  id,
  value,
  onChange,
  videoDevices,
  offLabel,
  unavailableLabel,
  placeholder,
  disabled,
  unset,
}: Props) {
  // Radix shows the placeholder for "".
  const selectValue =
    (unset || offLabel === undefined) && !value
      ? ""
      : selectValueFor(
          devicePathFor(value || null, videoDevices),
          videoDevices.map((device) => device.path),
          UNAVAILABLE_DEVICE_OPTION,
        );
  const selected = videoDevices.find((device) => device.path === selectValue);

  return (
    <Select
      value={selectValue}
      disabled={disabled}
      onValueChange={(next) => onChange(next === DISABLED_OPTION ? null : next)}
    >
      <SelectTrigger id={id} className="h-10 w-full min-w-0">
        {/* The name only; the list shows details to tell devices apart. */}
        <SelectValue
          className="min-w-0 flex-1 overflow-hidden text-left"
          placeholder={placeholder ?? offLabel}
        >
          {selected && <span className="truncate">{selected.label}</span>}
        </SelectValue>
      </SelectTrigger>
      <SelectContent>
        {offLabel !== undefined && (
          <SelectItem value={DISABLED_OPTION}>{offLabel}</SelectItem>
        )}
        {selectValue === UNAVAILABLE_DEVICE_OPTION && (
          <SelectItem value={UNAVAILABLE_DEVICE_OPTION} disabled>
            {unavailableLabel}
          </SelectItem>
        )}
        {videoDevices.length > 0 ? (
          videoDevices.map((device) => (
            <SelectItem key={device.path} value={device.path}>
              <DeviceLabel device={device} />
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

/** The device's label, with its details muted below. */
function DeviceLabel({ device }: { device: VideoDeviceInfo }) {
  return (
    <span className="flex min-w-0 flex-col">
      <span className="truncate">{device.label}</span>
      {device.details && (
        <span className="truncate text-xs text-muted-foreground">
          {device.details}
        </span>
      )}
    </span>
  );
}
