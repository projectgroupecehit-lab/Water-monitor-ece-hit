import { google } from "googleapis";

/* ================= ENV CHECK ================= */
if (!process.env.GOOGLE_SERVICE_ACCOUNT_JSON) {
  throw new Error("GOOGLE_SERVICE_ACCOUNT_JSON is missing");
}

/* ================= GOOGLE AUTH ================= */
const auth = new google.auth.GoogleAuth({
  credentials: JSON.parse(process.env.GOOGLE_SERVICE_ACCOUNT_JSON),
  scopes: ["https://www.googleapis.com/auth/spreadsheets"],
});

const sheets = google.sheets({ version: "v4", auth });

// IMPORTANT: Use only column A to force next-row append
const SPREADSHEET_ID = "1kLWxlQNOPLvHRZiXmWsB1SpzjSYuyfqUZnhdFpYlCdg";
const RANGE = "Sheet1!A:A";

export const appendToSheet = async ({ deviceId, temp, turb, do_val, tds, ec, ph }) => {
  try {

    /* ---------- TIME (IST) ---------- */
    const now = new Date();
    const dateIST = `'${now.toLocaleDateString("en-CA", {
      timeZone: "Asia/Kolkata",
    })}`;

    const timeIST = `'${now.toLocaleTimeString("en-GB", {
      timeZone: "Asia/Kolkata",
      hour12: false,
    })}`;


    /* ---------- ROW DATA ---------- */
    const row = [
      dateIST,      // A - Date (IST)
      timeIST,      // B - Time (IST)
      deviceId,     // C - Device ID
      do_val,       // D - DO
      ph,            // E - EC (optional)
      tds,          // F - TDS
      turb,         // G - Turbidity
      temp,         // H - Temperature
      ec            // I - pH (optional)
    ];


    /* ---------- APPEND ---------- */
    const res = await sheets.spreadsheets.values.append({
      spreadsheetId: SPREADSHEET_ID,
      range: RANGE,
      valueInputOption: "USER_ENTERED",
      insertDataOption: "INSERT_ROWS",
      requestBody: {
        values: [row],
      },
    });

    console.log(
      "Sheet updated at:",
      res.data.updates.updatedRange
    );

  } catch (err) {
    console.error("Sheet update failed:", err.message);
  }
};
