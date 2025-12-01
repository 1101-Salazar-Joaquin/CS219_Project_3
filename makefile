cpu: main.o cpu.o
	g++ -o cpu main.o cpu.o -g

main.o: main.cpp cpu.h
	gcc -c main.cpp -g 

cpu.o : cpu.cpp cpu.h
	gcc -c cpu.cpp -g 

clean:
	rm *.o cpu