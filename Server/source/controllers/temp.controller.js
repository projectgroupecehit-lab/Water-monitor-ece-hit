import { Temp } from "../models/temp.model.js";
import ApiError from "../utils/ApiError.js";
import ApiResponse from "../utils/ApiResponse.js";
import { appendToSheet } from "../utils/googleSheet.js";

const setdata = async (req, res) => {
  const { deviceId, temp, turb, do_val, tds, ec, ph } = req.body;
  console.log(req.body)

  try { 
    const tempData = await Temp.create({
      device_id: deviceId,
      temperature: Number(temp),
      turbidity: Number(turb),
      do: Number(do_val),
      tds: Number(tds),
      ph: Number(ph),
      ec: Number(ec),
    });

    appendToSheet({ deviceId, temp, turb, do_val, tds, ec, ph })
      .catch(err => console.error("Sheet Error:", err.message));

    return res.status(201).json(
      new ApiResponse(200, "Data saved to DB & Sheet", tempData, true)
    );

  } catch (error) {
    throw new ApiError(500, error.message);
  } 
};

export { setdata };
