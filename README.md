# Busy Buddy
Busy Buddy started off as a fun and extensible project that provides a way to visualize your Teams status, what is known as your “Presence” in Microsoft 365. Busy Buddy was able to focus on just the presentation side, the LED and screen, because of the fantastic work of Isaac Levin on his Presence Light project. This software, available via GitHub and the Microsoft Store, is an application that monitors your Teams Presence status and notifies devices [like Busy Buddy] when that status changes. Out of the box, Presence Light talks to several commercially available smart lights and also provides a custom interface, which is how it talks to Busy Buddy. As such, you need to have Presence Light installed if you want to use Busy Buddy to indicate you Teams status.

That said, a funny thing happened while building Busy Buddy – I was able to make it more generic so that it could be used to indicate the status of pretty much anything you can dream up. It has a simple REST API which accepts a POST that includes a parameter for the status text (“Free”, “Busy”, etc.), as well as a parameter for the color. This means that you can send a simple POST message to Busy Buddy to, for example, indicate that your DevOps build pipeline passed or failed, or that your garage door was left open, or pretty much anything along those lines. All the text is configurable, as well as the LED colors, and you can do that without ever having to modify and compile the source code.

The full build and setup instructions, bill of materials and other info can be found in a PDF document in the version (v1) folder. This covers both source and compiled bin options so if you really don't want to get into compiling stuff and just want a working Busy Buddy, that's an option too! I've also included a simple 3D printable case design that you'll find in the "case" folder.

Busy Buddy is a DIY project intended for educational and personal use only. It is not intended for commercial use or to be relied upon for any professional or medical purposes. The creators and contributors of Busy Buddy are not responsible for any damages or injuries that may result from the use of this project. By using the Busy Buddy software and/or hardware plans, you acknowledge that you understand and assume all risks associated with its use. Please use caution and common sense when working on any DIY project.

![20230128_211500532_iOS](https://user-images.githubusercontent.com/11561147/215344382-fe648c0e-acdb-42ee-a151-73144be2682b.jpg)
![20230214_004233884_iOS](https://user-images.githubusercontent.com/11561147/219048958-918e198b-5e9a-44be-9f60-696a0c186781.jpg)


## Links
Presence Light GitHub: https://github.com/isaacrlevin/presencelight

Presence Light Microsoft Store: https://apps.microsoft.com/store/detail/presencelight/9NFFKD8GZNL7
