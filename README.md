# Image Tagger Game
Program written and developed in the language C as the first project for the subject COMP30023: Computer Systems at the University of Melbourne - Semester 1, 2019.

### Goal:
To write a program to implement (by socket programming in C) a game server supporting two players from two different browsers over HTTP. The program should allow users to configure both the server IP address and the port number.

---

### The Rules of the Game
- The game consists of two players and one server, where each of the players can only communicate with the server (but the other player). 
- At the beginning, when the two players log on to the server, the server sends both of them a same image. The game then starts; the players submit words or phrases to the server, one at a time, without being told what the other player has inputed. 
- The goal for both the players is to enter a word or a phrase that has been submitted by the other as soon as possible.
- Obviously, in order to maximise the chance of getting a match, the two players would better to enter words that describe the image well, since it is the only information shared by both of the two. (The image is, therefore, tagged.) Once the goal is achieved, the game ends; the server sends a web page indicating that the game is completed and prompting the players to play again. 
- If both the players agree to play again, the process repeats with a new image.

### Tasks
- The game server must be implemented by socket programming in C.
- The server must use **HTTP protocol** to communicate with the client browsers.
- A makefile must be provided along with the code for compilation.
- The image_tagger program must accept two parameters: (i) a specified server IP address, and (ii) a specified port number.
- Redirect players to the pages with the _html source files_ provided.

---

### How to Run the Server
1. Linux environment is required in order to run the server.
2. Compile the code files using the makefile.
3. Run the server - **image_tagger.c**! 

##### How the site looks like:
- Start Screen:
  ![start-screen](https://github.com/nickangmc/image-tagger/blob/master/readme-images/start-screen.png)
  
- Ready Screen:
  ![ready-screen](https://github.com/nickangmc/image-tagger/blob/master/readme-images/ready-screen.png)


