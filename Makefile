OBJ=decache.o

TARGET=decache

OFLAGS=-O0 -ggdb
DEFINES=

inc=$(OBJ:%.o=%.d)

CFLAGS=$(OFLAGS) -Wall -Wextra -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wno-trigraphs \
	   -std=c11 -fno-strict-aliasing -fno-common -Werror-implicit-function-declaration -Wuninitialized \
	   -Wmissing-include-dirs -Wshadow -Wframe-larger-than=2047 -D_GNU_SOURCE \
	   -I. $(DEFINES)
LDFLAGS=

$(TARGET): $(OBJ)
	$(CC) -o $(TARGET) $(OBJ) $(LDFLAGS)

-include $(inc)

.c.o:
	$(CC) $(CFLAGS) -MMD -MP -c $<

clean:
	$(RM) $(OBJ) $(TARGET)
	$(RM) $(inc)

.PHONY: clean
