import mongoose from "mongoose"

const tempSchema = new mongoose.Schema({
    device_id: String,
    temperature: Number,
    ph: Number,
    tds: Number,
    do: Number,
    ec: Number,
    turbidity: Number

},{timestamps: true})


export const Temp = mongoose.model("Temp", tempSchema)