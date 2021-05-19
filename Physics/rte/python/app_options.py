import os
import sys

# Write an options file. This should be a separate routine
def write_template(args):
    app_dir = args.discharge_home + "/" + args.base_dir + "/" + args.app_name
    options_filename = app_dir + "/template.inputs"
    optf = open(options_filename, 'w')
    
    # Write plasma kinetics options
    options_files = [args.discharge_home + "/Source/AmrMesh/AmrMesh.options", \
                     args.discharge_home + "/Source/Driver/Driver.options", \
                     args.discharge_home + "/Source/RadiativeTransfer/" + args.rte_solver + ".options",\
                     args.discharge_home + "/Source/geometry/geo_coarsener.options", \
                     args.discharge_home + "/Geometries/" + args.geometry + "/" + args.geometry + ".options", \
                     args.discharge_home + "/Physics/rte/rte_stepper.options"]

    for opt in options_files:
        if os.path.exists(opt):
            f = open(opt, 'r')
            lines = f.readlines()
            optf.writelines(lines)
            optf.write('\n\n')
            f.close()
        else:
            print 'Could not find options file (this _may_ be normal behavior) ' + opt
    optf.close()
