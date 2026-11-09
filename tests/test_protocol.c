#define _POSIX_C_SOURCE 200809L

#include "protocol.h"
#include "net_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "check.h"




static void expect_invalid(const char *line, const char *reason){
    command_t command;

    CHECK(protocol_parse(line, &command) < 0);
    CHECK(command.type == C_INVALID);
    CHECK(strcmp(command.error, reason) == 0);

    if(strcmp(command.error, reason) != 0)
        printf("    \"%s\": expected \"%s\", got \"%s\"\n", line, reason, command.error);
}


static void test_valid_commands(void){
    check_section("valid commands", "las 6 órdenes se parsean con argumentos correctos");
    command_t command;

    CHECK(protocol_parse("LIST", &command) == 0 && command.type == C_LIST);
    CHECK(protocol_parse("SHUTDOWN", &command) == 0 && command.type == C_SHUTDOWN);

    CHECK(protocol_parse("STATUS 7", &command) == 0);
    CHECK(command.type == C_STATUS && command.id == 7);

    CHECK(protocol_parse("CANCEL 12", &command) == 0);
    CHECK(command.type == C_CANCEL && command.id == 12);

    CHECK(protocol_parse("SUBMIT 350", &command) == 0);
    CHECK(command.type == C_SUBMIT && command.body_len == 350);

    //The biggest YAML allowed.
    CHECK(protocol_parse("SUBMIT 1048576", &command) == 0 && command.body_len == MAX_YAML_BYTES);

    //Extra spaces between words are ignored.
    CHECK(protocol_parse("STATUS   3", &command) == 0 && command.id == 3);
}



static void test_logs(void){
    check_section("LOGS variants", "LOGS con job, FOLLOW y combinaciones");
    command_t command;

    CHECK(protocol_parse("LOGS 1", &command) == 0);
    CHECK(command.type == C_LOGS && command.id == 1 && command.job[0] == '\0' && command.follow == 0);

    CHECK(protocol_parse("LOGS 1 build", &command) == 0);
    CHECK(strcmp(command.job, "build") == 0 && command.follow == 0);

    CHECK(protocol_parse("LOGS 1 FOLLOW", &command) == 0);
    CHECK(command.job[0] == '\0' && command.follow == 1);

    CHECK(protocol_parse("LOGS 1 build FOLLOW", &command) == 0);
    CHECK(strcmp(command.job, "build") == 0 && command.follow == 1);

    expect_invalid("LOGS", "expected 1 to 3 arguments");
    expect_invalid("LOGS 1 build FOLLOW extra", "expected 1 to 3 arguments");
    expect_invalid("LOGS x", "invalid id");
    expect_invalid("LOGS 1 build follow", "expected FOLLOW");

    //JOB_NAME_MAX includes the '\0', so the longest name has JOB_NAME_MAX - 1 characters.
    char line[128];
    char name[JOB_NAME_MAX + 1];

    memset(name, 'a', JOB_NAME_MAX - 1);
    name[JOB_NAME_MAX - 1] = '\0';
    snprintf(line, sizeof line, "LOGS 1 %s", name);
    CHECK(protocol_parse(line, &command) == 0 && strlen(command.job) == JOB_NAME_MAX - 1);

    memset(name, 'a', JOB_NAME_MAX);
    name[JOB_NAME_MAX] = '\0';
    snprintf(line, sizeof line, "LOGS 1 %s", name);
    expect_invalid(line, "job name too long");
}



static void test_invalid_commands(void){
    check_section("malformed lines", "líneas incompletas o vacías se rechazan sin romperse");

    expect_invalid("", "empty command");
    expect_invalid("    ", "empty command");
    expect_invalid("HELLO", "unknown command");

    //Commands are case sensitive.
    expect_invalid("list", "unknown command");
    expect_invalid("submit 10", "unknown command");

    expect_invalid("LIST now", "command takes no arguments");
    expect_invalid("SHUTDOWN 1", "command takes no arguments");

    expect_invalid("STATUS", "expected 1 argument");
    expect_invalid("STATUS 1 2", "expected 1 argument");
    expect_invalid("CANCEL", "expected 1 argument");
    expect_invalid("SUBMIT", "expected 1 argument");
}


static void test_invalid_numbers(void){
    check_section("invalid ids and sizes", "ids no numéricos y longitudes negativas o infladas");

    expect_invalid("STATUS 0", "invalid id");
    expect_invalid("STATUS -3", "invalid id");
    expect_invalid("STATUS abc", "invalid id");
    expect_invalid("STATUS 12abc", "invalid id");
    expect_invalid("CANCEL 99999999999999999999999", "invalid id");

    expect_invalid("SUBMIT 0", "invalid yaml size");
    expect_invalid("SUBMIT -5", "invalid yaml size");
    expect_invalid("SUBMIT ten", "invalid yaml size");
    expect_invalid("SUBMIT 1048577", "invalid yaml size");
}


static void test_too_many_words(void){
    check_section("too many words", "argumentos de más se rechazan");

    expect_invalid("LOGS 1 a b c d e f g h", "too many arguments");
}




static void round_trip(int ok, const char *body){
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    CHECK(protocol_send_response(sockets[0], ok, body) == 0);

    int read_ok = -1;
    char *read_body = NULL;
    size_t len = 0;

    CHECK(protocol_read_response(sockets[1], &read_ok, &read_body, &len) == 0);
    CHECK(read_ok == ok);
    CHECK(len == strlen(body));
    CHECK(read_body != NULL && strcmp(read_body, body) == 0);

    free(read_body);
    close(sockets[0]);
    close(sockets[1]);
}


//Writes raw bytes as if they were a response and checks that reading them fails.
static void expect_bad_header(const char *raw){
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    CHECK(nu_write_all(sockets[0], raw, strlen(raw)) == 0);

    //Closing the writer makes a short body end in EOF instead of blocking.
    close(sockets[0]);

    int ok;
    char *body = NULL;
    size_t len;

    CHECK(protocol_read_response(sockets[1], &ok, &body, &len) < 0);

    close(sockets[1]);
}


static void test_responses(void){
    check_section("response framing", "cada respuesta es OK/ERR + n bytes exactos");

    round_trip(1, "7");
    round_trip(0, "invalid id");

    //Empty body: the end of logs --follow (OK 0).
    round_trip(1, "");

    //A body with several lines, like STATUS or a list of errors.
    round_trip(0, "line 1: unknown key 'x' in pipeline\nstages[1]: duplicate stage name 'b'\n");

    expect_bad_header("HELLO 3\nabc");     //neither OK nor ERR
    expect_bad_header("OK\n");             //no length
    expect_bad_header("OK abc\n");         //length is not a number
    expect_bad_header("OK -1\n");          //negative length
    expect_bad_header("OK 10\nabc");       //body shorter than announced
}


int main(void)
{
    check_header("protocol", "línea de comando del cliente y encapsulado de respuestas");
    test_valid_commands();
    test_logs();
    test_invalid_commands();
    test_invalid_numbers();
    test_too_many_words();
    test_responses();

    return check_summary("protocol — parseo y framing de comandos: OK/ERR + n bytes");
}