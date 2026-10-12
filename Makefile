all: scheduler/simpleshell scheduler/simplescheduler scheduler/sample_job pipe-shell/myshell

scheduler/simpleshell: scheduler/simpleshell.c scheduler/shared.h
	gcc -Wall -o $@ $<
scheduler/simplescheduler: scheduler/simplescheduler.c scheduler/shared.h
	gcc -Wall -o $@ $<
scheduler/sample_job: scheduler/sample_job.c scheduler/dummy_main.h
	gcc -include scheduler/dummy_main.h -o $@ $<
pipe-shell/myshell: pipe-shell/code.c
	gcc -Wall -o $@ $<

clean:
	rm -f scheduler/simpleshell scheduler/simplescheduler scheduler/sample_job pipe-shell/myshell

.PHONY: all clean
