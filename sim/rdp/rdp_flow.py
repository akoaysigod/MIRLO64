#!/usr/bin/env python3
"""A frame's flow through the RSP and the RDP, from the two benches' logs:
when the RSP hands each command word over (tb_rsp DPCT=<file>) and what the
RDP takes to draw between one word and the next (tb_rdp TB_CMDT=<file>, the
RDP alone with every word there from the start). The RDP draws a word when it
has come and the one before it is done; the frame ends with the last.

    rdp_flow.py rsp.txt rdp.txt RDP_CYCLES [rsp_scale rdp_scale]...

prints the end, the RDP's idle time and its backlog when the RSP ends, for the
logs as they are and for each pair of scales (0.8 0.7: an RSP that takes 80 %
of the time, an RDP that takes 70 %)."""
import sys

MHZ = 62.832


def run(arr, take, total, a, b):
    t = 0.0
    idle = 0.0
    for w in range(len(take)):
        ready = arr[w] * a
        if ready > t:
            idle += ready - t
            t = ready
        t += ((take[w + 1] if w + 1 < len(take) else total) - take[w]) * b
    return t, idle


def main():
    rsp = [tuple(map(int, l.split())) for l in open(sys.argv[1])]
    take = [int(l) for l in open(sys.argv[2])][::2]     # (the RDP takes a 64-bit word in two halves)
    total = int(sys.argv[3])
    arr = []
    for cyc, n in rsp:
        arr += [cyc] * (n - len(arr))
    n = min(len(arr), len(take))
    arr, take = arr[:n], take[:n]
    rsp_end = rsp[-1][0]
    pairs = [(1.0, 1.0)] + [(float(sys.argv[i]), float(sys.argv[i + 1])) for i in range(4, len(sys.argv) - 1, 2)]
    ms = lambda c: c / MHZ / 1000
    print(f"{n} command words; the RSP's last at {ms(rsp_end):.1f} ms; the RDP alone {ms(total):.1f} ms")
    for a, b in pairs:
        end, idle = run(arr, take, total, a, b)
        print(f"  RSP x{a:.2f}, RDP x{b:.2f}: the frame ends at {ms(end):5.1f} ms "
              f"(RSP {ms(rsp_end * a):.1f}, then {ms(end - rsp_end * a):.1f} of the RDP's; the RDP idle {ms(idle):.1f})")


if __name__ == "__main__":
    main()
