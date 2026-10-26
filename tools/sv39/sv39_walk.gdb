
# Lab2 Challenge B: Sv39 Page Table Walker
# GDB native command script (no Python required)
#
# Usage:
#   sv39walk 0xffffffffc02000d6
#
# Assumptions:
#   - RISC-V RV64, Sv39
#   - GDB can read the physical page-table addresses
#   - Run while the target is stopped

define sv39walk
    if $argc != 1
        printf "Usage: sv39walk <virtual_address>\n"
    else
        set $va = (unsigned long long)$arg0
        set $satp_value = (unsigned long long)$satp
        set $mode = ($satp_value >> 60) & 0xf

        printf "\n===== Sv39 Page Table Debugger =====\n"
        printf "Virtual address: 0x%016llx\n", $va
        printf "SATP: 0x%016llx\n", $satp_value

        if $mode != 8
            printf "ERROR: Sv39 is not enabled (MODE=%llu)\n", $mode
        else
            # Check Sv39 canonical virtual address
            set $sign = ($va >> 38) & 1
            set $upper = $va >> 39
            set $expected = 0

            if $sign == 1
                set $expected = 0x1ffffff
            end

            if $upper != $expected
                printf "ERROR: Non-canonical Sv39 virtual address\n"
            else
                # SATP: PPN occupies bits [43:0]
                set $root = ($satp_value & 0xfffffffffff) << 12

                set $vpn2 = ($va >> 30) & 0x1ff
                set $vpn1 = ($va >> 21) & 0x1ff
                set $vpn0 = ($va >> 12) & 0x1ff
                set $offset = $va & 0xfff

                printf "Mode: Sv39\n"
                printf "Root page table: 0x%016llx\n", $root
                printf "VPN[2]: %llu\n", $vpn2
                printf "VPN[1]: %llu\n", $vpn1
                printf "VPN[0]: %llu\n", $vpn0
                printf "Page offset: 0x%03llx\n", $offset

                set $table = $root
                set $level = 2
                set $finished = 0

                # Visit L2 -> L1 -> L0 until a leaf or error
                while $level >= 0 && $finished == 0
                    set $vpn = ($va >> (12 + 9 * $level)) & 0x1ff
                    set $pte_addr = $table + $vpn * 8

                    printf "\n[L%lld] Table PA: 0x%016llx\n", $level, $table
                    printf "[L%lld] VPN index: %llu\n", $level, $vpn
                    printf "[L%lld] PTE address: 0x%016llx\n", $level, $pte_addr

                    set $pte = *(unsigned long long *)$pte_addr

                    printf "[L%lld] PTE value: 0x%016llx\n", $level, $pte

                    set $v = $pte & 1
                    set $r = ($pte >> 1) & 1
                    set $w = ($pte >> 2) & 1
                    set $x = ($pte >> 3) & 1

                    printf "Flags: V=%llu R=%llu W=%llu X=%llu\n", $v, $r, $w, $x

                    # An invalid PTE or W=1/R=0 is rejected
                    if $v == 0 || ($w == 1 && $r == 0)
                        printf "ERROR: Invalid PTE\n"
                        set $finished = 1
                    else
                        # A PTE with R=1 or X=1 is a leaf
                        if $r == 1 || $x == 1
                            set $ppn = ($pte >> 10) & 0xfffffffffff
                            set $page_bits = 12 + 9 * $level
                            set $page_mask = (1ULL << $page_bits) - 1
                            set $align_mask = (1ULL << (9 * $level)) - 1

                            if ($ppn & $align_mask) != 0
                                printf "ERROR: Misaligned superpage\n"
                            else
                                set $pa = (($ppn << 12) & ~$page_mask) | ($va & $page_mask)

                                if $level == 2
                                    printf "Page size: 1 GiB\n"
                                else
                                    if $level == 1
                                        printf "Page size: 2 MiB\n"
                                    else
                                        printf "Page size: 4 KiB\n"
                                    end
                                end

                                printf "Physical address: 0x%016llx\n", $pa
                                printf "Translation successful!\n"
                            end

                            set $finished = 1
                        else
                            # Non-leaf PTE points to the next-level table
                            set $next_ppn = ($pte >> 10) & 0xfffffffffff
                            set $table = $next_ppn << 12

                            printf "Non-leaf PTE: descending to L%lld\n", $level - 1

                            set $level = $level - 1
                        end
                    end
                end

                if $finished == 0
                    printf "ERROR: No leaf PTE found\n"
                end
            end
        end

        printf "====================================\n\n"
    end
end

document sv39walk
Walk an RISC-V Sv39 page table and display virtual-to-physical translation.

Usage: sv39walk <virtual_address>

Features:
- Decode satp and VPN indices
- Walk L2, L1, L0 page tables
- Recognize 1 GiB, 2 MiB, 4 KiB leaf mappings
- Detect invalid PTEs and misaligned superpages
- Calculate the translated physical address
end
