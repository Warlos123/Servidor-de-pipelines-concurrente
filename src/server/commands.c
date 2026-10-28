#define _POSIX_C_SOURCE 200809L
 
#include "../includes/commands.h"
#include "../includes/job_table.h"
#include "../includes/dispatcher.h"
#include <stdio.h>


void command_execute(const command_t *command, char *reply, size_t size){

    job_t job;
    unsigned long position = 0;

    switch (command->type)
    {

    //SUBMIT
    case C_SUBMIT: {
        unsigned long id = 0;

        switch (job_submit(&command->params, &id))
        {
        case SUBMIT_OK:
            snprintf(reply, size, "OK %lu", id);
            break;
        
        case SUBMIT_BUSY:
            snprintf(reply, size, "BUSY");
            break;

        case SUBMIT_STOPPING:
            snprintf(reply, size, "ERROR server shutting down");
            break;

        default:
            snprintf(reply, size, "ERROR job table full");
            break;
        }

        break;
    }

    //STATUS
    case C_STATUS:
        if(job_get(command->id, &job, &position) < 0){
            snprintf(reply, size, "ERROR unknown job");
            break;
        }

        switch (job.status)
        {
        case JOB_QUEUED:
            snprintf(reply, size, "QUEUED %lu", position);
            break;
        
        case JOB_RUNNING: {
            int step = 0, total = job.params.steps;
            dispatcher_get_status(command->id, &step, &total);
            snprintf(reply, size, "RUNNING %d/%d", step, total);
            break;
        }
        
        case JOB_DONE:
            snprintf(reply, size, "DONE %.3f", job.seconds);
            break;

        default:
            snprintf(reply, size, "ERROR job failed");
            break;
        }

        break;

    //PEEK
    case C_PEEK:
        if (job_get(command->id, &job, &position) < 0)
            snprintf(reply, size, "ERROR unknown job");

        else if (command->i >= job.params.nx || command->j >= job.params.ny)
            snprintf(reply, size, "ERROR cell out of grid");

        else if(job.status == JOB_QUEUED)
            snprintf(reply, size, "ERROR job not started");

        else if(job.status == JOB_ERROR)
            snprintf(reply, size, "ERROR job failed");

        else {
            int step;
            double val;
            
            switch (dispatcher_peek(command->id, command->i, command->j, &step, &val))
            {
            case 0:
                snprintf(reply, size, "VALUE %d %.10e", step, val);
                break;
            
            case -2:
                snprintf(reply, size, "ERROR cell out of grid");
                break;
            
            case -3: 
                snprintf(reply, size, "ERROR no snapshot");
                break;

            default:
                snprintf(reply, size, "ERROR snapshot not available");
                break;
            }

        }
            

        break;


    //RESULT
    case C_RESULT:
        if(job_get(command->id, &job, &position) < 0)
            snprintf(reply, size, "ERROR unknown job");

        else if (job.status != JOB_DONE)
            snprintf(reply, size, "ERROR job not finished");

        else    
            snprintf(reply, size, "RESULT %.10e %s", job.sum_abs_ez, job.file); //NEEDS FILE

        break;


    //SHUTDOWN
    case C_SHUTDOWN:
        snprintf(reply, size, "BYE"); 
        server_request_shutdown();
        break;

    
    default:
        snprintf(reply, size, "ERROR");
        break;
    }
}