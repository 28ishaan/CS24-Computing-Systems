from collections import namedtuple, defaultdict
import argparse
import os.path
from textwrap import wrap

parser = argparse.ArgumentParser(description="Grade Project04a")
parser.add_argument("-s", "--stage", type=int, choices=[1,2,3,4], required=True, help="Grade this stage")
args = parser.parse_args()

KERNEL_BOUNDARY = 256
PM_SIZE = 512
VM_SIZE = 768

if not os.path.isfile("log.txt"):
    print("No output from run, did code compile?")
    assert False

def print_page_info(pg, owner, usable):
    print(f"      Page #: {pg} | Owner: {owner} | User-accessible: {usable}")

def parse_physical_dump(s: str):
    result = [a for a in s]
    if len(result) != PM_SIZE:
        print("FAIL: Physical memory dump is not the expected size")
        print("      Did the code finish running?")
    return result

def parse_virtual_dump(s: str) :
    result = [(a, b == "u") for (a, b) in zip(s[::2], s[1::2])]
    if len(result) != VM_SIZE:
        print("FAIL: Virtual memory dump is not the expected size")
        print("      Did the code finish running?")
        assert False
    return result

def confirm_physical_exhausted(pm):
    # Checks that physical address space is exhausted
    if any(filter(lambda x: x == ".", pm)):
        print("FAIL: The entirety of physical memory needs to be allocated")
        print("      This error was thrown because your physical memory does")
        print("      not allocate physical frames from 0x0 to 0x200000,")
        print("      which it should if stage 4 was correctly implemented.")
        assert False

def confirm_virtual_allocation(pm):
    # Checks that physical address space is being used up for virtual allocation
    for i in range(0, KERNEL_BOUNDARY + 64 * 2 + 7):
        if pm[i] == ".":
            print("FAIL: Virtual allocation is not allocating enough")
            print("      This error was thrown because your physical memory does")
            print("      not allocate physical frames from 0x0 to 0x187000,")
            print("      which it should if stage 3 was correctly implemented.")
            assert False

def confirm_kernel_isolation(vm):
    # Checks that all of the virtual address spaces have it such that
    # kernel"s addresses are not accessible  from any of the processes
    # (except for the CGA block). Also check that the CGA page is accessible
    # from all processes.
    for i, (p, u) in enumerate(vm):
        if i == 184:
            if p != "C" or not u:
                print("FAIL: Console memory must be accessible by user process")
                print("      This error was thrown because your virtual address")
                print("      block at 0xB8000 did not point to the console or")
                print("      it was not mapped as usable by the user process.")
                print_page_info(i, p, u)
                assert False
        elif i < KERNEL_BOUNDARY and u:
            print("FAIL: Kernel memory must not be accessible by user process")
            print("      This error was thrown because your virtual address")
            print("      blocks below the kernel boundary was marked as accessible")
            print("      by the user process.")
            print_page_info(i, p, u)
            assert False
        elif p == "." and u:
            print("FAIL: Unmapped page is accessible")
            print_page_info(i, p, u)
            assert False

def confirm_process_isolation(vm, pid):
    # Check that beyond the kernel memory map, processes only pages mapped
    # for itself.
    for i in range(KERNEL_BOUNDARY, VM_SIZE):
        (p, u) = vm[i]
        if p != "." and (p != pid or not u):
            print(f"FAIL: Process memory is not properly isolated for process {pid}")
            print("      This error was thrown because your virtual address")
            print(f"      blocks for the process {pid} below the kernel boundary")
            print(f"      was owned by someone else other than the process {pid}.")
            print_page_info(i, p, u)
            assert False

def confirm_stack_address_end(vm, pid):
    i = VM_SIZE - 1
    (p, u) = vm[i]
    if p != pid:
        print(f"FAIL: Process {pid} stack does not start at MEMSIZE_VIRTUAL")
        print("      This error was thrown because your virtual address")
        print(f"      block at MEMSIZE_VIRTUAL - PAGESIZE is not mapped to")
        print(f"      a page owned by process {pid}.")
        print_page_info(i, p, u)
        assert False

def confirm_overlap(vms):
    # Check that there is overlap in virtual address spaces
    def check_overlap(vm1, vm2):
        for ((a, _), (b, _)) in zip(vm1, vm2):
            if a != "." and b != "." and a != b:
                return True
        return False

    if not any(check_overlap(vma, vmb) for vma in vms for vmb in vms if vma is not vmb):
        print("FAIL: Virtual memory pages did not overlap")
        print("      This error was thrown because your virtual address blocks")
        print("      blocks below the kernel boundary did not overlap with")
        print("      other virtual address blocks of other processes, which it")
        print("      should if stage 4 was completed successfully.")
        assert False

def confirm_frequency(vcs):
    if vcs[0] >= vcs[1] or vcs[1] >= vcs[2] or vcs[2] >= vcs[3]:
        print("FAIL: Unexpected virtual mapped page frequency")
        print("      This error was thrown because your virtual address blocks")
        print("      allocated for PID 1, 2, 3, 4 did not have it such that")
        print("      count1 < count2 < count3 < count4")
        assert False

def count_physical_pages_owners(pm):
    m = dict()
    for p in pm:
        if p in m:
            m[p] += 1
        else:
            m[p] = 1
    return m

def count_virtual_pages(vm, pid):
    count = 0
    for i in range(KERNEL_BOUNDARY, VM_SIZE):
        (p, u) = vm[i]
        if p == pid:
            count += 1
    return count

def confirm_counts(pcount, vmc, pid, delta):
    if vmc != pcount[pid] - delta:
        print(f"WARNING: number of pages mapped for virtual ({pcount[pid] - delta}) and physical ({vmc}) do not match for process {pid}")
        print(f"   NOTE: This may fail if you have implemented a later stage than the one you are testing for")

def read_log_file():
    with open("log.txt") as f:
        return [line.rstrip("\n") for line in f.readlines()]

lines = read_log_file()
try:        
    # parsing
    if (lines[0] != "Starting WeensyOS"
        or lines[1] != "PHYSICAL MEMORY DUMP"
        or lines[3] != "VIRTUAL MEMORY FOR PROCESS 1"
        or lines[5] != "VIRTUAL MEMORY FOR PROCESS 2"
        or lines[7] != "VIRTUAL MEMORY FOR PROCESS 3"
        or lines[9] != "VIRTUAL MEMORY FOR PROCESS 4"):
        print("FAIL: Testing output mismatch, did you leave in debugging log_printf in your code?")
        assert False
    
    # parsing
    pm = parse_physical_dump(lines[2])
    vm1 = parse_virtual_dump(lines[4])
    vm2 = parse_virtual_dump(lines[6])
    vm3 = parse_virtual_dump(lines[8])
    vm4 = parse_virtual_dump(lines[10])

    pcount = count_physical_pages_owners(pm)
    vm1c = count_virtual_pages(vm1, "1")
    vm2c = count_virtual_pages(vm2, "2")
    vm3c = count_virtual_pages(vm3, "3")
    vm4c = count_virtual_pages(vm4, "4")

    print(f"Checking for stage {args.stage} completion:")

    if args.stage == 1:
        confirm_counts(pcount, vm1c, "1", 0)
        confirm_counts(pcount, vm2c, "2", 0)
        confirm_counts(pcount, vm3c, "3", 0)
        confirm_counts(pcount, vm4c, "4", 0)
    elif args.stage == 2 or args.stage == 3:
        confirm_counts(pcount, vm1c, "1", 4)
        confirm_counts(pcount, vm2c, "2", 4)
        confirm_counts(pcount, vm3c, "3", 4)
        confirm_counts(pcount, vm4c, "4", 4)
    elif args.stage == 4:
        confirm_counts(pcount, vm1c, "1", 5)
        confirm_counts(pcount, vm2c, "2", 5)
        confirm_counts(pcount, vm3c, "3", 5)
        confirm_counts(pcount, vm4c, "4", 5)

    # checking
    if args.stage >= 1:
        print("Checking stage 1...")
        confirm_kernel_isolation(vm1)
        confirm_kernel_isolation(vm2)
        confirm_kernel_isolation(vm3)
        confirm_kernel_isolation(vm4)
    if args.stage >= 2:
        print("Checking stage 2...")
        confirm_process_isolation(vm1, "1")
        confirm_process_isolation(vm2, "2")
        confirm_process_isolation(vm3, "3")
        confirm_process_isolation(vm4, "4")
    if args.stage >= 3:
        print("Checking stage 3...")
        confirm_virtual_allocation(pm)
    if args.stage >= 4:
        print("Checking stage 4...")
        confirm_physical_exhausted(pm)
        confirm_overlap([vm1, vm2, vm3, vm4])
        confirm_stack_address_end(vm1, "1")
        confirm_stack_address_end(vm2, "2")
        confirm_stack_address_end(vm3, "3")
        confirm_stack_address_end(vm4, "4")
        # confirm_frequency([vm1c, vm2c, vm3c, vm4c])
    
    print(f"Tests up to stage {args.stage} passed!")
except AssertionError:
    print("Tests failed!")
    # Dump out the memory view
    print(lines[0])
    print(lines[1])
    for line in wrap(lines[2],64):
        print(line)
    print(lines[3])
    for line in wrap(lines[4],128):
        print(line)
    print(lines[5])
    for line in wrap(lines[6],128):
        print(line)
    print(lines[7])
    for line in wrap(lines[8],128):
        print(line)
    print(lines[9])
    for line in wrap(lines[10],128):
        print(line)
    exit(1)
except BaseException as e:
    print(f"Unknown exception: {e}")