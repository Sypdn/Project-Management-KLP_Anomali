type TandonData = {
  distance_cm?: number;
  level_cm?: number;
  level_percent: number;
  pump_status?: string;
  mode?: string;
};

function analyze(data: TandonData) {
  let status: "RENDAH" | "NORMAL" | "PENUH" = "NORMAL";

  if (data.level_percent <= 20) {
    status = "RENDAH";
  } else if (data.level_percent >= 90) {
    status = "PENUH";
  }

  return { status };
}


export default analyze;