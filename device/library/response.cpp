/* Read various data and command files and write
   the compressed request output. */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <bits/stdc++.h>

#include "common.h"
using namespace std;

/* remove all null characters from string */
void trim_string(string &input) {
    input.erase(std::remove(input.begin(), input.end(), '\0'),
            input.end());
}

uint64_t remove_null_chars(char* tmp, char* trim, uint64_t size) {
    int i = 0;
    uint64_t final_size = 0;
    char* cbuff_ptr = tmp;
    char* trim_cbuff_ptr = trim;
    for (i = 0 ; i < size; i++) {
        if (*cbuff_ptr != '\0') {
            *trim_cbuff_ptr = *cbuff_ptr;
            trim_cbuff_ptr++;
            final_size++;
        }
        cbuff_ptr++;
    }
    return final_size;
}

/* store the current log locally in a separate file. */
void dump_log (void) {
    string line;
    uint64_t start = log_file_start_offset;
    uint64_t bytes = log_file_end_offset - log_file_start_offset;
    unsigned long line_count = 0;

    /* random sanity check for less than 100 MB reads. */
    if (bytes >= 100*1024*1024) {
        cout << "error: size to read is greater than 100 MB!\n";
        return;
    }

#if 0
    /* open the log file and seek to the start of our read. */
    ifstream log_file (log_filename);
    if (!log_file.is_open()) {
        cout << "error: couldn't open log file." << endl;
        return;
    }
    log_file.seekg(start, log_file.beg);
#endif

    FILE* log_file = fopen(log_filename.c_str(), "r");
    if (!log_file) {
        cout << "error: could not open the log file.\n";
        return;
    }
    fseek(log_file, log_file_start_offset, SEEK_SET);

    FILE* resp_file = fopen(tmp_resp_filename.c_str(), "w");
    if (!log_file) {
        cout << "error: could not open the response file.\n";
        return;
    }

    /* read in chunks of 128 KB */
    size_t tmpbuf_size = 128*1024;
    char* tmpbuf = (char*) malloc(tmpbuf_size);
    if (!tmpbuf) {
        cout << "error: couldn't allocate a buffer.\n";
        return;
    }

    char* trimbuf = (char*) malloc(tmpbuf_size);
    if (!trimbuf) {
        cout << "error: couldn't allocate a buffer.\n";
        return;
    }

#if 0
    cout << "log start offset = " << log_file_start_offset << endl;
    cout << "log end offset = " << log_file_end_offset << endl;
    cout << "bytes to read = " << log_file_end_offset-log_file_start_offset << endl;
#endif

    size_t readbytes = 0;
    size_t trimbytes = 0;
    while (bytes != 0) {
        /* read from the log file */
        readbytes = fread(tmpbuf, 1, tmpbuf_size, log_file);
        if (readbytes == 0) break;
        
        /* trim the buffer */
        trimbytes = remove_null_chars(tmpbuf, trimbuf, tmpbuf_size);

        /* write trimmed buffer to file */
        fwrite(trimbuf, 1, trimbytes, resp_file);
        
        if (readbytes > bytes) {
            bytes = 0;
            break;
        }
        bytes -= readbytes;
    }
    fclose(log_file);
    fclose(resp_file);

#if 0
    /* open a new response file. */
    ofstream resp_file (tmp_resp_filename);
    if (!resp_file.is_open()) {
        cout << "error: could not open the response file (" << resp_filename << ").\n";
        return;
    }

    /* dump data from the log to the response file */
    size_t read_bytes = 0;
    if (resp_file.is_open())
    {
        while ( getline (log_file, line) )
        {
            /* remove null characters */
            trim_string(line);

            resp_file << line << '\n';
            line_count++;

            read_bytes += line.size();
            if (read_bytes >= bytes) break;
        }
        log_file.close();
        resp_file.close();
    }

    /* This condition is triggered but it is likely due to some weird bookk and write to fileeeping issue. */
    if (read_bytes < bytes) {
        cout << "error: read only " << read_bytes << " bytes (actual: " << bytes << ")\n";
    }
#endif

    if (bytes > 0) {
        cout << "error: read only " << readbytes << " bytes (remaining: " << bytes << ")\n";
    }

    // cout << "success: dumping log complete.\n";
    free(tmpbuf);
    free(trimbuf);

    return;
}

/* clear a portion of the log file (TEST) */
bool clear_log(uint64_t start, uint64_t bytes) {

    /* open the raw log file and seek to the start of our read */
    ofstream log_file (log_filename, fstream::out);
    if (!log_file.is_open()) {
        cout << "error: couldn't open the log file." << endl;
        return false;
    }
    log_file.seekp(start);

    /* create a buffer of bytes size and set it to 0 */
    char* cleanbuf = (char*) malloc(bytes);
    if (!cleanbuf) {
        cout << "error: couldn't clean buffer of size " << bytes << endl;
        return false;
    }
    memset(cleanbuf, 0, bytes);

    /* write the clean buffer to the position */
    log_file << cleanbuf << endl;
    log_file.close();

    return false;
}

/* Filter on the basis of a key */
void filter_and_dump_log(string key) {
    string line;
    uint64_t start = log_file_start_offset;
    uint64_t bytes = log_file_end_offset - log_file_start_offset;

    ifstream log_file (log_filename);
    if (!log_file.is_open()) {
        cout << "error: couldn't open log file (" << log_filename <<  ")." << endl;
        return;
    }
    log_file.seekg(start, log_file.beg);

    ofstream resp_file (tmp_resp_filename);
    if (!resp_file.is_open()) {
        cout << "error: could not open the response file (" << resp_filename << ").\n";
        return;
    }

    int total = 0;
    size_t read_bytes = 0;
    while ( getline (log_file, line) )
    {
        if (line.find(key) != string::npos) {
            resp_file << line << '\n';
            total++;
        }

        read_bytes += line.size();
        if (read_bytes >= bytes) break;
    }
    log_file.close();
    resp_file.close();

   cout << "Filter: on key '" << key << "', there were " << total << " events\n";
}

void commit(void) {
    if (compression == true)
        commit_compress();
    
    /* sign the log file (TODO) */

    /* append data to the response file */
    size_t bytes = commit_response();

    /* let the host know data is ready */
    commit_response_message(bytes);

    /* reset the filename for next requests */
    tmp_resp_filename = "response";
}

void commit_compress(void) {
    /* compress the tmp. response file (if required) */
    string cmp_cmd = "pigz --fast -k -f " + tmp_resp_filename; 
    system(cmp_cmd.c_str());
    tmp_resp_filename += ".gz";
    cout << "success: compression complete (file: " << tmp_resp_filename << ").\n";
}

size_t commit_response (void) {

#if 0
    /* get the tmp. response file, which should be compressed and signed by now. */
    ifstream tmp_resp_file (tmp_resp_filename);
    if (!tmp_resp_file.is_open()) {
        cout << "error: could not open the log file.\n";
        return 0;
    }
    cout << "success: opened the log file.\n";


    /* open the USB response file and seek to its start. */
    fstream resp_file;
    resp_file.open(resp_filename, fstream::out);
    if (!resp_file.is_open()) {
        cout << "error: could not open the response file.\n";
        return 0;
    }
    resp_file.seekp(0);
#endif

    /* get the tmp. response file, which should be compressed and signed by now. */
    FILE* tmp_resp_file = fopen(tmp_resp_filename.c_str(), "r");
    if (!tmp_resp_file) {
        cout << "error: could not open the log file.\n";
        return 0;
    }
    cout << "success: opened the log file.\n";

    FILE* resp_file = fopen(resp_filename.c_str(), "r+");
    if (!resp_file) {
        cout << "error: could not open the response file.\n";
        return 0;
    }
    cout << "success: opened the response file.\n";

#if 0
    /* write to the USB response file */
    string line;
    size_t line_count = 0;
    size_t byte_count = 0;
    while ( getline (tmp_resp_file,line) )
    {
        fwrite(line.c_str(), line.size(), 1, resp_file);
        line_count++;
        byte_count += line.size();
    }
    fclose(resp_file);
    tmp_resp_file.close();
#endif

    size_t readbuf_size = 128*1024;
    char* readbuf = (char*) malloc(readbuf_size);
    if (!readbuf) {
        cout << "error: couldn't allocate read buffer.\n";
        return 0;
    }
    memset(readbuf, 0, readbuf_size);

    size_t bytes = 0;
    size_t byte_count = 0;
    while (true) {
        bytes = fread(readbuf, 1, readbuf_size, tmp_resp_file);
        if (bytes <= 0) break;
        fwrite(readbuf, 1, readbuf_size, resp_file);
        byte_count += bytes;
    }
    fclose(resp_file);
    fclose(tmp_resp_file);

    cout << "success: written " << byte_count << " bytes to " << resp_filename << ".\n";
    return byte_count;
}

void commit_response_message (size_t bytes) {
    /* Let the host know that the response is now ready. 
       FORMAT = "READY <bytes>" */

    string resp_msg = "READY " + to_string(bytes);
    cout << "Response: " << resp_msg << endl;

#if 0
    /* open the USB command file and seek to its start. */
    fstream comm_file;
    comm_file.open(comm_filename, std::ios::app);
    if (!comm_file.is_open()) {
        cout << "error: could not open the command file.\n";
        return;
    }
    comm_file.seekp(0);
    comm_file << resp_msg << endl;
    comm_file.close();
#endif

    FILE* fp;
    if (!(fp = fopen(comm_filename.c_str(), "r+"))) {
        cout << "error: could not open the command file.\n";
        return;
    };
    fwrite(resp_msg.c_str(), resp_msg.size(), 1, fp);
    fclose(fp);

    cout << "success: told the host that data is ready.\n";
    return;
}
