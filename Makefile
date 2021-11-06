CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -g -O
INCLUDES = 
LIBS = -laudiofile -lm
SRCS = mixramp.c gain_analysis.c
OBJS = $(SRCS:.c=.o)
MAIN = mixramp

all: $(MAIN)

$(MAIN): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) -o $(MAIN) $(OBJS) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	$(RM) *.o *~ $(MAIN)
