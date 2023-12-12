#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "directory_tree.h"
#include "fat16.h"

const size_t MASTER_BOOT_RECORD_SIZE = 0x20B;

void follow(FILE *disk, directory_node_t *node, bios_parameter_block_t bpb) {
    directory_entry_t entry;
    // Keeps going until encountering an entry beginning with '\0'
    while (true) {
        assert(fread(&entry, sizeof(entry), 1, disk) == 1);

        // Checks if name starts with '\0'
        if (entry.filename[0] == '\0') {
            break;
        }

        // Current offset
        long curr_offset = ftell(disk);

        // Fseek
        size_t offset = get_offset_from_cluster(entry.first_cluster, bpb);
        fseek(disk, offset, SEEK_SET);
        // Skips hidden entries
        if (is_hidden(entry)) {
            fseek(disk, curr_offset, SEEK_SET);
            continue;
        }
        // Directory Entry
        else if (is_directory(entry)) {
            // Creates new directory node, attaches it to parent, recursively calls.
            directory_node_t *new_dnode = init_directory_node(get_file_name(entry));
            add_child_directory_tree(node, (node_t *) new_dnode);
            follow(disk, new_dnode, bpb);
        }
        // File Entry
        else {
            // Reads the file size
            uint8_t *file_size = malloc(sizeof(uint8_t) * entry.file_size);
            assert(fread(file_size, entry.file_size, 1, disk) == 1);
            // Creates a new file node, attaches it to parent.
            file_node_t *new_fnode =
                init_file_node(get_file_name(entry), entry.file_size, file_size);
            add_child_directory_tree(node, (node_t *) new_fnode);
        }
        // Reset location
        fseek(disk, curr_offset, SEEK_SET);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "USAGE: %s <image filename>\n", argv[0]);
        return 1;
    }

    FILE *disk = fopen(argv[1], "r");
    if (disk == NULL) {
        fprintf(stderr, "No such image file: %s\n", argv[1]);
        return 1;
    }

    // Skip past Master Boot Record
    fseek(disk, MASTER_BOOT_RECORD_SIZE, SEEK_SET);

    // Fread BPB into BPB
    bios_parameter_block_t bpb;
    assert(fread(&bpb, sizeof(bpb), 1, disk) == 1);

    // Skip past padding to the beginning of root directory
    size_t location = get_root_directory_location(bpb);
    fseek(disk, location, SEEK_SET);

    directory_node_t *root = init_directory_node(NULL);
    follow(disk, root, bpb);
    print_directory_tree((node_t *) root);
    create_directory_tree((node_t *) root);
    free_directory_tree((node_t *) root);

    int result = fclose(disk);
    assert(result == 0);
}
