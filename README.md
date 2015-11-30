# ceu-remote-events-binding
This project aims to create an environment to Céu scripts emit events that are received
by other Ceu scripts.

# Dependencies
This project depends on [GLib 2.0](https://developer.gnome.org/glib/) and [Lua 5.2](http://www.lua.org/). 
Both dependencies can be installed on Ubuntu using the default source repository:

* apt-get install libglib2.0-dev liblua5.2-dev 

# Compile
To compile any sample in samples folder, one should use the following command:

* make CEUFILE=samples/<sample_file>

