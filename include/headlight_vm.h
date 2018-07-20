#ifndef HEADLIGHT_VM_H
#define HEADLIGHT_VM_H

void resume_headlight_vm(int rc);
void suspend_headlight_vm(void);
inline void request_checkpoint(void);

#endif
