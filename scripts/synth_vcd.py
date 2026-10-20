#!/usr/bin/env python3
"""Генератор СИНТЕТИЧНИХ VCD-сигналів для перевірки конвеєра розбору (decode -> check -> vectors).

Це НЕ записи з заліза і не здаються як результат ЛР2 — лише тестові фікстури scripts/fixtures/.
Канали названі D0, D1, ... як у записах fx2lafw, щоб decode_captures.sh працював однаково.

  python3 scripts/synth_vcd.py            # створює scripts/fixtures/*.vcd
"""
import pathlib

OUT = pathlib.Path(__file__).resolve().parent / "fixtures"
TICK_NS = 1000          # 1 мкс на відлік (timescale 1 us)


class Sig:
    def __init__(self, names, init):
        self.names, self.t, self.state, self.events = names, 0, dict(init), []
        for n, v in init.items():
            self.events.append((0, n, v))

    def set(self, **kw):
        for n, v in kw.items():
            if self.state[n] != v:
                self.state[n] = v
                self.events.append((self.t, n, v))

    def wait(self, us):
        self.t += us

    def vcd(self):
        ids = {n: chr(33 + i) for i, n in enumerate(self.names)}
        out = ["$timescale 1 us $end", "$scope module top $end"]
        out += [f"$var wire 1 {ids[n]} {n} $end" for n in self.names]
        out += ["$upscope $end", "$enddefinitions $end"]
        last = None
        for t, n, v in sorted(self.events, key=lambda e: e[0]):
            if t != last:
                out.append(f"#{t}")
                last = t
            out.append(f"{v}{ids[n]}")
        out.append(f"#{self.t}")
        return "\n".join(out) + "\n"


# ---------- I2C / SCCB (D0 = SCL, D1 = SDA), період біта = 10 мкс ----------
def i2c(txns, bit_us=10):
    """txns: список (список байтів, ack_bits) ; ack_bits — що відповідає слейв на 9-му біті (0 = ACK)."""
    s = Sig(["D0", "D1"], {"D0": 1, "D1": 1})
    h = bit_us // 2
    s.wait(50)
    for data, acks in txns:
        s.set(D1=0); s.wait(h); s.set(D0=0); s.wait(h)                 # START
        for byte, ack in zip(data, acks):
            for i in range(7, -1, -1):
                s.set(D1=(byte >> i) & 1); s.wait(h)
                s.set(D0=1); s.wait(bit_us); s.set(D0=0); s.wait(h)
            s.set(D1=ack); s.wait(h)                                    # 9-й біт
            s.set(D0=1); s.wait(bit_us); s.set(D0=0); s.wait(h)
        s.set(D1=0); s.wait(h); s.set(D0=1); s.wait(h); s.set(D1=1)    # STOP
        s.wait(200)
    return s


# ---------- SPI mode 0 (D0 = SCK, D1 = MISO, D2 = MOSI, D3 = CS#) ----------
def spi(frames, bit_us=10):
    """frames: список (mosi_bytes, miso_bytes) однакової довжини."""
    s = Sig(["D0", "D1", "D2", "D3"], {"D0": 0, "D1": 0, "D2": 0, "D3": 1})
    h = bit_us // 2
    s.wait(50)
    for mosi, miso in frames:
        s.set(D3=0); s.wait(bit_us)
        for tx, rx in zip(mosi, miso):
            for i in range(7, -1, -1):
                s.set(D2=(tx >> i) & 1, D1=(rx >> i) & 1); s.wait(h)
                s.set(D0=1); s.wait(bit_us); s.set(D0=0); s.wait(h)
        s.set(D3=1); s.wait(100)
    return s


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    ACK, NACK = 0, 1
    # I2C: MPU-6050, запис вказівника регістра 0x3B + STOP, далі читання 6 байтів (master ACK на 1..5, NACK на 6-му)
    burst = [0x00, 0x40, 0xFF, 0xE0, 0x3F, 0xE0]
    (OUT / "i2c_mpu6050_synthetic.vcd").write_text(i2c([
        ([0xD0, 0x75], [ACK, ACK]),                                  # WHO_AM_I: addr W + reg
        ([0xD1, 0x68], [ACK, NACK]),                                 # addr R + дані 0x68, master NACK
        ([0xD0, 0x3B], [ACK, ACK]),
        ([0xD1] + burst, [ACK] + [ACK] * 5 + [NACK]),
    ]).vcd())
    # SCCB: 2-фазний запис адреси регістра PID (0x0A) + STOP, 2-фазне читання 0x76 (9-й біт читання від master = NACK)
    (OUT / "sccb_ov7670_synthetic.vcd").write_text(i2c([
        ([0x42, 0x0A], [ACK, ACK]),
        ([0x43, 0x76], [ACK, NACK]),
    ]).vcd())
    # SPI: JEDEC 9F -> EF 40 17
    (OUT / "spi_w25q_synthetic.vcd").write_text(spi([
        ([0x9F, 0, 0, 0], [0, 0xEF, 0x40, 0x17]),
    ]).vcd())
    print("fixtures ->", OUT)
