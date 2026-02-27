all:
	gcc -g -fsanitize=address -o ./bin/main ./code/main.c -Icode/abs/include
run:
	./bin/main
clean:
	rm -rf ./bin/*
install:
	mv ./bin/main ~/.local/bin/abs