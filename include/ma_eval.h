#ifndef _MA_EVAL_H_
#define _MA_EVAL_H_

extern int checkpoint_requested;
#define EVAL
#ifdef EVAL

#define NUM_SAMPLES	10000
extern uint64_t req_chkpt_time[NUM_SAMPLES];
extern uint64_t time_suspended[NUM_SAMPLES];
extern int run_eval;

extern uint64_t t_resume_start, t_resume_vcan, t_resume_app, t_resume_before_vcan;

void resume_eval_vm(int rc);
void suspend_eval_vm(void); 

#endif
#endif
