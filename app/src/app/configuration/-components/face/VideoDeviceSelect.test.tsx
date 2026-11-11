import { expect, test } from "bun:test";
import { renderToStaticMarkup } from "react-dom/server";
import { VideoDeviceSelect } from "./VideoDeviceSelect";

test("the selected IR camera shows its node and stable path, even with the same RGB label", () => {
  const rgbPath = "/dev/v4l/by-id/usb-Logitech_BRIO-video-index0";
  const irPath = "/dev/v4l/by-id/usb-Logitech_BRIO-video-index2";
  const videoDevices = [rgbPath, irPath].map((path, index) => ({
    path,
    node: `/dev/video${2 + index * 2}`,
    aliases: [path, `/dev/video${2 + index * 2}`],
    name: "Logitech BRIO",
    label: "Logitech BRIO",
    details: "",
  }));
  const html = renderToStaticMarkup(
    <VideoDeviceSelect
      value={irPath}
      onChange={() => {}}
      videoDevices={videoDevices}
      unavailableLabel="IR camera not connected"
    />,
  );
  expect(html).toContain("Logitech BRIO — /dev/video4");
  expect(html).toContain(`title="${irPath}"`);
});
