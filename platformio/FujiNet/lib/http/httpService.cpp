#include <Arduino.h>
#include <WiFi.h>
#include <vector>
#include <map>
#include <sstream>

#include "httpService.h"
#include "httpServiceParser.h"
#include "printer.h"
#include "../../src/main.h"

using namespace std;

/* Send some meaningful(?) error message to client
*/
void fnHttpService::return_http_error(httpd_req_t *req, _fnwserr errnum)
{
    const char * message;
    
    switch(errnum)
    {
        case fnwserr_fileopen:
            message = "Error opening file";
            break;
        case fnwserr_memory:
            message = "Ran out of memory";
            break;
        default:
            message = "Unexpected web server error";
            break;
    }
    httpd_resp_send(req, message, strlen(message));
}


const char * fnHttpService::find_mimetype_str(const char *extension)
{
    static std::map<std::string, std::string> mime_map {
        {"css", "text/css"},
        {"png", "image/png"},
        {"jpg", "image/jpeg"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"pdf", "application/pdf"},
        {"ico", "image/x-icon"},
        {"txt", "text/plain"},
        {"bin", "application/octet-stream"},
        {"atascii", "application/octet-stream"}
    };

    if(extension != NULL) 
    {
        std::map<std::string, std::string>::iterator mmatch;

        mmatch = mime_map.find(extension);
        if (mmatch != mime_map.end())
            return mmatch->second.c_str();
    }
    return NULL;
}


char * fnHttpService::get_extension(const char *filename)
{
    char *result = strrchr(filename, '.');
    if (result != NULL)
        return ++result;
    return NULL;
}

/* Set the response content type based on the file being sent.
*  Just using the file extension
*  If nothing is set here, the default is 'text/html'
*/
void fnHttpService::set_file_content_type(httpd_req_t *req, const char *filepath)
{
    // Find the current file extension
    char * dot = get_extension(filepath);
    if(dot != NULL)
    {
        const char *mimetype = find_mimetype_str(dot);
        if(mimetype)
            httpd_resp_set_type(req, mimetype);
    }
}

/* Send file content after parsing for replaceable strings
*/
void fnHttpService::send_file_parsed(httpd_req_t *req, const char *filename)
{
    // Note that we don't add FNWS_FILE_ROOT as it should've been done in send_file()
#ifdef DEBUG
        Debug_printf("Opening file for parsing: '%s'\n", filename);
#endif
    
    _fnwserr err = fnwserr_noerrr;

    // Retrieve server state
    serverstate * pState = (serverstate *) httpd_get_global_user_ctx(req->handle);
    File fInput = pState->pFS->open(filename, "r");

    if (!fInput || !fInput.available()) 
    {
#ifdef DEBUG
        Debug_println("Failed to open file for parsing");
#endif
        err = fnwserr_fileopen;
    }
    else
    {
        // Set the response content type
        set_file_content_type(req, filename);
        // We're going to load the whole thing into memory, so watch out for big files!
        size_t sz = fInput.size() + 1;
        char * buf = (char *)malloc(sz);
        if(buf == NULL)
        {
            #ifdef DEBUG
                Debug_printf("Couldn't allocate %u bytes to load file contents!\n", sz);
            #endif
            err = fnwserr_memory;
        }
        else
        {
            memset(buf, 0, sz); // Make sure we have a zero terminator
            fInput.readBytes(buf, sz);
            string contents(buf);
            free(buf);
            contents = fnHttpServiceParser::parse_contents(contents);

            httpd_resp_send(req, contents.c_str(), contents.length());            
        }
    }

    if(fInput) 
        fInput.close();

    if(err != fnwserr_noerrr)
        return_http_error(req, err);
}

/* Send content of given file out to client
*/
void fnHttpService::send_file(httpd_req_t *req, const char *filename)
{
    // Build the full file path
    string fpath = FNWS_FILE_ROOT;
    // Trim any '/' prefix before adding it to the base directory
    while(*filename == '/')
        filename++;
    fpath += filename;

    // Handle file differently if it's one of the types we parse
    if(fnHttpServiceParser::is_parsable(get_extension(filename)))
        return send_file_parsed(req, fpath.c_str());

    // Retrieve server state
    serverstate * pState = (serverstate *) httpd_get_global_user_ctx(req->handle);

    File fInput = pState->pFS->open(fpath.c_str(), "r");
    if (!fInput || !fInput.available()) {
#ifdef DEBUG
        Debug_printf("Failed to open file for sending: '%s'\n", fpath.c_str());
#endif        
        return_http_error(req, fnwserr_fileopen);
    } else {
        // Set the response content type
        set_file_content_type(req, fpath.c_str());
        // Set the expected length of the content
        char hdrval[10];
        snprintf(hdrval, 10, "%u", fInput.size());
        httpd_resp_set_hdr(req, "Content-Length", hdrval);

        // Send the file content out in chunks    
        char *buf = (char *)malloc(FNWS_SEND_BUFF_SIZE);
        size_t count = 0;
        do 
        {
            count = fInput.read((uint8_t *)buf, FNWS_SEND_BUFF_SIZE);
            httpd_resp_send_chunk(req, buf, count);
        } while(count > 0);
        fInput.close();
        free(buf);
    }
}

void fnHttpService::parse_query(httpd_req_t *req, queryparts *results)
{
    results->full_uri += req->uri;
    // See if we have any arguments
    int path_end = results->full_uri.find_first_of('?');
    if(path_end < 0)
    {
        results->path += results->full_uri;
        return;
    }
    results->path += results->full_uri.substr(0, path_end -1);
    results->query += results->full_uri.substr(path_end +1);
    // TO DO: parse arguments, but we've no need for them yet
}

esp_err_t fnHttpService::get_handler_index(httpd_req_t *req)
{
#ifdef DEBUG
    Debug_println("Index request handler");
#endif

    send_file(req, "index.html");
    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_file_in_query(httpd_req_t *req)
{
#ifdef DEBUG
    Debug_printf("File_in_query request handler '%s'\n", req->uri);
#endif

    // Get the file to send from the query
    queryparts qp;
    parse_query(req, &qp);
    send_file(req, qp.query.c_str());

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_file_in_path(httpd_req_t *req)
{
#ifdef DEBUG
    Debug_printf("File_in_path request handler '%s'\n", req->uri);
#endif

    // Get the file to send from the query
    queryparts qp;
    parse_query(req, &qp);
    send_file(req, qp.path.c_str());

    return ESP_OK;
}

esp_err_t fnHttpService::get_handler_print(httpd_req_t *req)
{
#ifdef DEBUG
    Debug_println("Print request handler");
#endif

    // A bit of a kludge for now: get printer from main routine
    sioPrinter *currentPrinter = getCurrentPrinter();

    // Build a print output name
    const char *exts;
    // Choose an extension based on current printer papertype
    switch (currentPrinter->getPaperType())
    {
        case RAW:
            exts = "bin";
            break;
        case TRIM:
            exts = "atascii";
            break;
        case ASCII:
            exts = "txt";
            break;
        case PDF:
            exts = "pdf";
            break;
        case SVG:
            exts = "svg";
            break;
        default:
            exts = "bin";
    }
    string filename = "printout.";
    filename += exts;
    // Set the expected content type based on the filename/extension
    set_file_content_type(req, filename.c_str());

    // Flush and close the print output before continuing
    currentPrinter->flushOutput();
    // Add a couple of attchment-specific details
    char hdrval1[60];
    snprintf(hdrval1, 60, "attachment; filename=\"%s\"", filename.c_str());
    httpd_resp_set_hdr(req, "Content-Disposition", hdrval1);
    char hdrval2[10];
    snprintf(hdrval2, 10, "%u", currentPrinter->getOutputSize());
#ifdef DEBUG
    Debug_printf("Printer says there's %u bytes in the output file\n", currentPrinter->getOutputSize());
#endif    
    httpd_resp_set_hdr(req, "Content-Length", hdrval2);

    // Finally, write the data
    // Send the file content out in chunks    
    char *buf = (char *)malloc(FNWS_SEND_BUFF_SIZE);
    size_t count = 0;
    do 
    {
        count = currentPrinter->readFromOutput((uint8_t *)buf, FNWS_SEND_BUFF_SIZE);
#ifdef DEBUG
        Debug_printf("Read %u bytes from print file\n", count);
#endif    
        httpd_resp_send_chunk(req, buf, count);
    } while(count > 0);
    free(buf);

    // Tell the printer it can start writing from the beginning
    currentPrinter->resetOutput();

#ifdef DEBUG
    Debug_println("Print request completed");
#endif
    return ESP_OK;
}

/* Set up and start the web server
*/
void fnHttpService::httpServiceInit()
{
    state.pFS = &SPIFFS;
    state.hServer = NULL;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Store some data we'll need when executing static functions
    config.global_user_ctx = (void *) &state;

    // Start the httpd server
#ifdef DEBUG
    Debug_printf("Starting web server on port %d\n", config.server_port);
#endif    
    if (httpd_start(&(state.hServer), &config) == ESP_OK) {
        // Register URI handlers
        for (const httpd_uri_t uridef : uris)
            httpd_register_uri_handler(state.hServer, &uridef);
        return;
    }

    state.hServer = NULL;
#ifdef DEBUG
    Debug_println("Error starting web server!");
#endif
}
