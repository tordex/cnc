import termctl

ctl = termctl.TemperatureControl("/dev/ttyUSB0")

while True:
    print("Enter command: ")
    cmd = raw_input()
    args = cmd.split(" ")
    if args[0] == "g":
        if len(args) != 2:
            print "g <0|1>"
            continue
        print ctl.get_data(int(args[1]))
    elif args[0] == "s":
        if len(args) != 3:
            print "s <0|1> <temp>"
            continue
        ctl.set_temperature(int(args[1]), int(args[2]))
        print "Temperature was set"
    elif args[0] == "tune":
        if len(args) != 3:
            print "tune <0|1> <temp>"
            continue
        ctl.start_tuning(int(args[1]), float(args[2]))
        print "Tuning is started"
    elif args[0] == "notune":
        if len(args) != 2:
            print "notune <0|1>"
            continue
        ctl.stop_tuning(int(args[1]))
        print "Tuning is started"
    elif args[0] == "settune":
        if len(args) != 5:
            print "notune <0|1> <Kp> <Ki> <Kd>"
            continue
        ctl.set_tuning(int(args[1]), float(args[2]), float(args[3]), float(args[4]))
        print "Tuning parameters are updated"
    else:
        print "Supported commands g, s, tune, notune, settune"

