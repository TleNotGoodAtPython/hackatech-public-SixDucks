This project is from The Hackathon called Hackatech which is a Hackathon event based on BKK Governor 
Elections combining with Youth and Technologies to make BKK a better place.
Organized by CIA CreativeLab, CreativeLabTH, and Resilience Collective Thailand (in collaboration with the MIT Urban Risk Lab)

We (SixDucks) made a project called "Thinkkhaya" which is an IoT-based  Trash bin that can convert trashes to points in order to reduce
the amount of trash in street or to make people put the bin into the trash.

For Our brief Architecture
<img width="1159" height="550" alt="image" src="https://github.com/user-attachments/assets/becb3bbe-0a01-48c5-b2ac-7d3cd02eb73f" />

For WebServer:
All endpoints html structure will be left in "Templates" (FRONTEND)
For Backend/api  we use Flask.py which is in app.py and PostgreSQL

For Hardware design.
We use ESP-32 and CYD (Cheap Yellow Display) combining with button and 2 ultrasonic sensors.

ESP-32: The core microcontroller handling IoT communication (Via ESP-NOW).
Brief Demonstration 

CYD (Cheap Yellow Display): Used for the on-bin user interface and recieving Points via QR.

 <img width="300" height="400" alt="image" src="https://github.com/user-attachments/assets/962aaa18-6cf6-4630-a742-0404c75f2a44" />

2x Ultrasonic Sensors: Used to detect trash being dropped in (RED) and to monitor bin capacity (BLUE).
<img width="300" height="400" alt="image" src="https://github.com/user-attachments/assets/cb90a1cb-a779-4967-947e-05c2ad82c7da" />

Brief Placement.

Button: To start/end user session.

<img width="300" height="400" alt="image" src="https://github.com/user-attachments/assets/72baf558-9493-4e5f-9c46-34132b2b587c" />

