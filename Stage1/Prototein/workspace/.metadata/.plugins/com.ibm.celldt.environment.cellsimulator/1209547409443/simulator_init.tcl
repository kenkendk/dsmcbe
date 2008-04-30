###############################################################################
# Copyright (c) 2006 IBM Corporation.
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the Eclipse Public License v1.0
# which accompanies this distribution, and is available at
# http://www.eclipse.org/legal/epl-v10.html
# 
# Contributors:
#     IBM Corporation - Initial Implementation
#
###############################################################################

#
# This file is executed within systemsim-cell. Do not execute this file directly from shell.
#

##
# Prints a token and a message that is recognized by Eclipse Cell IDE simulator plugin.
# @param tagname the token that identifies the message
# @param messagetext the message itself
#
proc CellDT_Status { tagname messagetext } {
	puts "||| $tagname: $messagetext |||"
}
CellDT_Status "INIT" "Parse"


##
# Prints an error message that is recognized by Eclipse Cell IDE simulator plugin.
# @param genericmessage the error message
# @param errormessage a message reported by TCL ou a called script
#
proc CellDT_Error { genericmessage {errormessage "" } } {
	if { [ string length $errormessage ] == 0 } {
		CellDT_Status "ERROR"  "$genericmessage"
		exit 1
	} else {
		CellDT_Status "ERROR" "$genericmessage Reported error: $errormessage"
		exit 1
	}	
}

if { ! [ info exists env(SYSTEMSIM_TOP) ] } {
	CellDT_Error "Environment variable is missing: SYSTEMSIM_TOP. This TCL file should be interpreted by systemsim."
}

if { ! [ info exists env(IMAGES_DIR) ] } {
	CellDT_Error "Environment variable is missing: IMAGES_DIR. This TCL file should be interpreted by systemsim."
}

if { ! [ info exists env(LIB_DIR) ] } {
	CellDT_Error "Environment variable is missing: LIB_DIR. This TCL file should be interpreted by systemsim."
}

##
# Ensures that a required file exists and is readable. If not, terminates simulator
# with error message that will be recognized by Eclipse Cell IDE simulator plugin.
# @param filename path to the file to be checked
# @param description describes the file in the error message.
#
proc CellDT_CheckFile { filename {description "" } } {
	if { ! [ file exists $filename ] } {
		if { [ string length $description ] == 0 } {
			CellDT_Error "Cannot find file $filename. Check if path is correct."
		} else {
			CellDT_Error "Cannot find file for $description ($filename). Check if path is correct."
		}
	}
	if { ! [ file readable $filename ] } {
		if { [ string length $description ] == 0 } {
			CellDT_Error "Cannot read file $filename. Check read permissions."
		} else {
			CellDT_Error "Cannot read file for $description ($filename). Check read permissions."
		}
	}
}

proc CellDT_RetrieveLaunchConfiguration { } {
	global env
	global SYSTEMSIM_TOP
	global CELLDT_VAR_DIR
	global CELLDT_WORKDIR
	global CELLDT_USERNAME
	global CELLDT_USERID
	
	puts "LAUNCH INFORMATION:"
	set CELLDT_WORKDIR [ file normalize . ]
	set CELLDT_VAR_DIR [ file normalize $CELLDT_WORKDIR/runinfo ]
	puts "   * systemsim base directory: $SYSTEMSIM_TOP"
	puts "   * Working directory: $CELLDT_WORKDIR"
	puts "   * Launch information directory: $CELLDT_VAR_DIR"
	if {![file writable $CELLDT_WORKDIR ]} {
		CellDT_Error "Simulator working directory is not writeable ($CELLDT_WORKDIR). Check write permissions."
	}
	if {[file exists $CELLDT_VAR_DIR ]} {
		CellDT_Error "There is already a simulator running in the working directory ($CELLDT_WORKDIR). Otherwise, remove the directory $CELLDT_VAR_DIR."
	}
	set result [ 
		catch {
			exec id -un
		} CELLDT_USERNAME
	]
	if { $result != 0 } {
		set errormessage $CELLDT_USERNAME
		CellDT_Error "Could not retrieve user information." $errormessage
	}
	set result [ 
		catch {
			exec id -u
		} CELLDT_USERID
	]
	if { $result != 0 } {
		set errormessage $CELLDT_USERID
		CellDT_Error "Could not retrieve user information." $errormessage
	}
	puts "   * User name: $CELLDT_USERNAME, $CELLDT_USERID"
	puts ""

}

proc CellDT_RetrieveMachineConfiguration { } {
	global env
	global CELLDT_CPU_CONFIG
	global CELLDT_MEMORY_SIZE
	
	puts "MACHINE CONFIGURATION:"
	if { [ info exists env(CELLDT_CPU_CONFIG) ] } {
		puts "   * Customized CPU"
		set CELLDT_CPU_CONFIG $env(CELLDT_CPU_CONFIG)
	} else {
		puts "   * Default CPU (single processor Cell)"
		set CELLDT_CPU_CONFIG "myconf config cider be_mode 6; myconf config cider bridge_type 0"
	}
	if { [ info exists env(CELLDT_MEMORY_SIZE) ] } {
		set CELLDT_MEMORY_SIZE $env(CELLDT_MEMORY_SIZE)
	} else {
		set CELLDT_MEMORY_SIZE 256
	}
	puts "   * Memory: $CELLDT_MEMORY_SIZE MB"
	puts ""
}

proc CellDT_RetrieveFileSystemConfiguration { } {
	global env
	global CELLDT_WORKDIR
	global CELLDT_KERNEL_IMAGE
	global CELLDT_ROOT_IMAGE
	global CELLDT_ROOT_PERSISTENCE
	global CELLDT_ROOT_JOURNAL
	global CELLDT_EXTRA_IMAGE
	global CELLDT_EXTRA_PERSISTENCE
	global CELLDT_EXTRA_JOURNAL
	global CELLDT_EXTRA_INIT
	global CELLDT_EXTRA_MOUNTPOINT
	global CELLDT_EXTRA_TYPE
	
	# Check for kernel image
	puts "FILE SYSTEM:"
	puts "   * Kernel:"
	if { [ info exists env(CELLDT_KERNEL_IMAGE) ] } {
		set CELLDT_KERNEL_IMAGE [ file normalize $env(CELLDT_KERNEL_IMAGE) ]
	} else {
		set filename $CELLDT_WORKDIR/vmlinux
		puts "     checking $filename..."
		
		if { [ file exists $filename ] } {
			set CELLDT_KERNEL_IMAGE $filename
		} else {
			set filename $env(IMAGES_DIR)/cell/vmlinux 
			puts "     checking $filename..."
			if { [ file exists $filename ] } {
				set CELLDT_KERNEL_IMAGE $filename
			} else {
				CellDT_Error "No operating system kernel image file found."
			}
		}
	}
	puts "     using  $CELLDT_KERNEL_IMAGE"
	CellDT_CheckFile $CELLDT_KERNEL_IMAGE "operating system kernel image"
	
	# Check for root file system
	puts "   * Root file system:"
	if { [ info exists env(CELLDT_ROOT_IMAGE) ] } {
		set CELLDT_ROOT_IMAGE [ file normalize $env(CELLDT_ROOT_IMAGE) ]
	} else {
		set filename $CELLDT_WORKDIR/sysroot_disk
		puts "     checking $filename..."
		
		if { [ file exists $filename ] } {
			set CELLDT_ROOT_IMAGE $filename
		} else {
			set filename $env(IMAGES_DIR)/cell/sysroot_disk 
			puts "     checking $filename..."
			if { [ file exists $filename ] } {
				set CELLDT_ROOT_IMAGE $filename
			} else {
				CellDT_Error "No root file system image file found."
			}
		}
	}
	puts "     using $CELLDT_ROOT_IMAGE"
	CellDT_CheckFile $CELLDT_ROOT_IMAGE "root file system image"
	if { [ info exists env(CELLDT_ROOT_PERSISTENCE) ] } {
		set CELLDT_ROOT_PERSISTENCE $env(CELLDT_ROOT_PERSISTENCE)
	} else {
		set CELLDT_ROOT_PERSISTENCE "discard"
	}
	switch $CELLDT_ROOT_PERSISTENCE {
		"discard" {
			puts "     (discard changes when simulator exists)"
		}
		"write" {
			puts "     (write changes directly on image file)"
			if {![file writable $CELLDT_ROOT_IMAGE ]} {
				CellDT_Error "Root file system image is not writeable ($CELLDT_ROOT_IMAGE). Check write permissions."
			}
		}
		"journal" {
			if { [ info exists env(CELLDT_ROOT_JOURNAL) ] } {
				set CELLDT_ROOT_JOURNAL $env(CELLDT_ROOT_JOURNAL)
			} else {
				set CELLDT_ROOT_JOURNAL "$CELLDT_WORKDIR/[ file tail $CELLDT_ROOT_IMAGE ].changes"
			}
			puts "     (journal changes to $CELLDT_ROOT_JOURNAL)"
		}
		default {
			CellDT_Error "Invalid value for CELLDT_ROOT_PERSISTENCE: $CELLDT_ROOT_PERSISTENCE."
		}
	}
	
	# Check for alternative file system
	if { [ info exists env(CELLDT_EXTRA_IMAGE) ] } {
		set CELLDT_EXTRA_IMAGE [ file normalize $env(CELLDT_EXTRA_IMAGE) ]
		set CELLDT_EXTRA_INIT true

		if { [ info exists env(CELLDT_EXTRA_TYPE) ] } {
			set CELLDT_EXTRA_TYPE $env(CELLDT_EXTRA_TYPE)
		} else {
			set CELLDT_EXTRA_TYPE "ext2"
		}
		switch $CELLDT_EXTRA_TYPE {
			"ext2" -
			"iso9660" {
				# Ok, accept.	
			}
			default {
				CellDT_Error "Invalid value for CELLDT_EXTRA_TYPE: $CELLDT_EXTRA_TYPE."
			}
		}
		if { [ info exists env(CELLDT_EXTRA_MOUNTPOINT) ] } {
			set CELLDT_EXTRA_MOUNTPOINT $env(CELLDT_EXTRA_MOUNTPOINT)
		} else {
			set CELLDT_EXTRA_MOUNTPOINT "/mnt"
		}
		puts "   * Extra file system: $CELLDT_EXTRA_IMAGE ($CELLDT_EXTRA_TYPE) on $CELLDT_EXTRA_MOUNTPOINT"

		if { [ info exists env(CELLDT_EXTRA_PERSISTENCE) ] } {
			set CELLDT_EXTRA_PERSISTENCE $env(CELLDT_EXTRA_PERSISTENCE)
		} else {
			set CELLDT_EXTRA_PERSISTENCE "discard"
		}
		switch $CELLDT_EXTRA_PERSISTENCE {
			"readonly" {
				puts "     (readonly)"
			}
			"discard" {
				puts "     (discard changes when simulator exists)"
			}
			"write" {
				puts "     (write changes directly on image file)"
				if {![file writable $CELLDT_EXTRA_IMAGE ]} {
					CellDT_Error "Extra file system image is not writeable ($CELLDT_EXTRA_IMAGE). Check write permissions."
				}
			}
			"journal" {
				if { [ info exists env(CELLDT_EXTRA_JOURNAL) ] } {
					set CELLDT_EXTRA_JOURNAL $env(CELLDT_EXTRA_JOURNAL)
				} else {
					set CELLDT_EXTRA_JOURNAL "$CELLDT_WORKDIR/[ file tail $CELLDT_EXTRA_IMAGE ].changes"
				}
				puts "     (journal changes to $CELLDT_EXTRA_JOURNAL)"
			}
			default {
				CellDT_Error "Invalid value for CELLDT_EXTRA_PERSISTENCE: $CELLDT_EXTRA_PERSISTENCE."
			}
		}
	} else {
		set CELLDT_EXTRA_INIT false
	}
}

proc CellDT_RetrieveSwitches { } {
	global env
	global CELLDT_CONSOLE_PORT
	global CELLDT_CONSOLE_INIT
	global CELLDT_CONSOLE_ECHO
	global CELLDT_CONSOLE_COMMANDS
	global CELLDT_NET_INIT

	if { [ info exists env(CELLDT_CONSOLE_PORT) ] } {
		set CELLDT_CONSOLE_PORT $env(CELLDT_CONSOLE_PORT) 
		set CELLDT_CONSOLE_INIT true
	} else { 
		set CELLDT_CONSOLE_INIT false
	}

	if { [ info exists env(CELLDT_CONSOLE_ECHO) ] } { 
		set CELLDT_CONSOLE_ECHO $env(CELLDT_CONSOLE_ECHO)
	} else { 
		set CELLDT_CONSOLE_ECHO true 
	}

	if { [ info exists env(CELLDT_NET_INIT) ] } { 
		set CELLDT_NET_INIT $env(CELLDT_NET_INIT)
	} else { 
		set CELLDT_NET_INIT false 
	}
	
	if { [ info exists env(CELLDT_CONSOLE_COMMANDS) ] } { 
		set CELLDT_CONSOLE_COMMANDS $env(CELLDT_CONSOLE_COMMANDS)
	} else { 
		set CELLDT_CONSOLE_COMMANDS "# No customization" 
	}

	puts "SWITCHES:"
	if { $CELLDT_CONSOLE_INIT } {
		puts "   * Wait for console on port: $CELLDT_CONSOLE_PORT"
	} else {
		puts "   * Don't redirect console to socket."
	}
	if { $CELLDT_CONSOLE_ECHO } {
		puts "   * Echo console output."
	} else {
		puts "   * Don't echo console output."
	} 
	if { $CELLDT_NET_INIT } {
		puts "   * Use bogusnet."
	} else {
		puts "   * Don't use bogusnet."
	}
	puts ""
}

	
proc CellDT_RetrieveBogusnet { } {
	global env
	global CELLDT_NET_INIT
	global CELLDT_NET_IP_HOST
	global CELLDT_NET_IP_SIMULATOR
	global CELLDT_NET_MAC_SIMULATOR
	global CELLDT_NET_MASK

	puts "BOGUS NET:"
	if { $CELLDT_NET_INIT } {
		if { [ info exists env(CELLDT_NET_IP_HOST) ] } {
			set CELLDT_NET_IP_HOST $env(CELLDT_NET_IP_HOST) 
		} else { 
			set CELLDT_NET_IP_HOST "172.20.0.1"
		}
		if { [ info exists env(CELLDT_NET_IP_SIMULATOR) ] } {
			set CELLDT_NET_IP_SIMULATOR $env(CELLDT_NET_IP_SIMULATOR) 
		} else { 
			set CELLDT_NET_IP_SIMULATOR "172.20.0.2"
		}
		if { [ info exists env(CELLDT_NET_MASK) ] } {
			set CELLDT_NET_MASK $env(CELLDT_NET_MASK) 
		} else { 
			set CELLDT_NET_MASK "255.255.0.0"
		}
		if { [ info exists env(CELLDT_NET_MAC_SIMULATOR) ] } { 
			set CELLDT_NET_MAC_SIMULATOR $env(CELLDT_NET_MAC_SIMULATOR) 
		} else {
			# TODO: generate automatically
			set CELLDT_NET_MAC_SIMULATOR "00:01:6C:EA:A0:23" 
		}
		puts "   * Host IP address: $CELLDT_NET_IP_HOST"
		puts "   * Simulator IP address: $CELLDT_NET_IP_SIMULATOR"
		puts "   * Simulator MAC address: $CELLDT_NET_MAC_SIMULATOR"
		
		# Check if there is already on interface using the ip address.
		# Grep returns 0 for success (found matching ip address) and 1 for failure
		set result [ 	
			catch {
				set has_ip [ exec /sbin/ifconfig | grep -c "addr:$CELLDT_NET_IP_HOST" ]
			} addresscount
		]
		if { $result == 0 } {
			CellDT_Error "There is already an interface using address range $CELLDT_NET_IP_HOST."
		}
		set result [ 	
			catch {
				set has_ip [ exec /sbin/ifconfig | grep -c "addr:$CELLDT_NET_IP_SIMULATOR" ]
			} addresscount
		]
		if { $result == 0 } {
			CellDT_Error "There is already an interface using address range $CELLDT_NET_IP_SIMULATOR."
		}
		if { ! [ file exists /dev/net/tun ] } {
			CellDT_Error "The device /dev/net/tun does not exist."
		}
	} else {
		puts "   * Don't use bogusnet."
	}
	puts ""
}


proc CellDT_RetrieveSSHLaunch { } {
	global env
	global CELLDT_WORKDIR
	global CELLDT_NET_INIT
	global CELLDT_SSH_INIT
	puts "SSH SERVER:"

	if { $CELLDT_NET_INIT } {
		if { [ info exists env(CELLDT_SSH_INIT) ] } {
			set CELLDT_SSH_INIT $env(CELLDT_SSH_INIT)
		} else {
			set CELLDT_SSH_INIT "false"
		}
		
		switch $CELLDT_SSH_INIT {
			"true" {
				puts "   * $CELLDT_WORKDIR/configure.sh"
				puts "   * $CELLDT_WORKDIR/ssh_host_dsa_key"
				puts "   * $CELLDT_WORKDIR/ssh_host_dsa_key.pub"
				puts "   * $CELLDT_WORKDIR/ssh_host_rsa_key"
				puts "   * $CELLDT_WORKDIR/ssh_host_rsa_key.pub"
				puts "   * $CELLDT_WORKDIR/sshd_config"
			
				CellDT_CheckFile $CELLDT_WORKDIR/ssh_host_dsa_key
				CellDT_CheckFile $CELLDT_WORKDIR/ssh_host_dsa_key.pub
				CellDT_CheckFile $CELLDT_WORKDIR/ssh_host_rsa_key
				CellDT_CheckFile $CELLDT_WORKDIR/ssh_host_rsa_key.pub
				CellDT_CheckFile $CELLDT_WORKDIR/sshd_config
			}
			"false" {
				puts "   * Don't launch sshserver in systemsim."
			}
			default {
				CellDT_Error "Invalid value for CELLDT_SSH_INIT: $CELLDT_SSH_INIT."
			}
		}

	} else {
		set CELLDT_LAUNCH_SSH false
		puts "   * Don't launch sshserver in systemsim (not possible without bogusnet)."
	}
}

# ---------------------------------------------------------------------------------------
# PARAMETER VALIDATION:
# Retrieve parameters given by the environment variables.
# Set default values for missing parameters that are optional.
# Validate values of parameters and verify the environment.
#
# Image files are first searched in the path provided in environment variables,
# then on the current directory, then on systemsim-cell default image directory.

CellDT_Status "INIT" "Check"

set SYSTEMSIM_TOP $env(SYSTEMSIM_TOP)

puts "*******************************************************************************"
puts "* PARAMETER RETIVAL AND VALIDATION                                            *"
puts "* Following configuration will be used to launch the Cell Simulator           *"
puts "*******************************************************************************"

CellDT_RetrieveLaunchConfiguration
CellDT_RetrieveMachineConfiguration
CellDT_RetrieveFileSystemConfiguration
CellDT_RetrieveSwitches
CellDT_RetrieveBogusnet
CellDT_RetrieveSSHLaunch

puts "*******************************************************************************"

# ---------------------------------------------------------------------------------------
# CONFIG LOG:
# Save configuration log
set result [
	catch {
		exec mkdir -p $CELLDT_VAR_DIR
		exec echo $CELLDT_USERNAME > $CELLDT_VAR_DIR/USERNAME
		exec echo $CELLDT_USERID > $CELLDT_VAR_DIR/USERID
		exec echo [ pid ] > $CELLDT_VAR_DIR/PID
				
		if { $CELLDT_NET_INIT } {
			exec echo $CELLDT_NET_MAC_SIMULATOR > $CELLDT_VAR_DIR/NET_MAC_SIMULATOR
			exec echo $CELLDT_NET_IP_HOST > $CELLDT_VAR_DIR/NET_IP_HOST
			exec echo $CELLDT_NET_IP_SIMULATOR > $CELLDT_VAR_DIR/NET_IP_SIMULATOR
		}
		
		exec echo "cd $CELLDT_VAR_DIR\nkill -9 \$(cat PID)\nif \[ -f TAP_DEVICE \]\n   then $SYSTEMSIM_TOP/bin/snif -d \$(cat TAP_DEVICE)\nfi\nrm -rf ../runinfo\ncd .." > $CELLDT_VAR_DIR/cleanup.sh
		exec chmod a+x $CELLDT_VAR_DIR/cleanup.sh
		
	} error_message
]
if { $result != 0 } {
	CellDT_Error "Could not save configuration log in $CELLDT_VAR_DIR." $error_message
}

# ---------------------------------------------------------------------------------------
# CONFIGURE THE MACHINE
# Following instructions are taken from .systemsim.tcl provided by the cell-sdk
CellDT_Status "INIT" "Configure"
	
# Initialize the systemsim tcl environment
source $env(LIB_DIR)/cell/mambo_init.tcl
	
# Configure and create the simulated machine
define dup cell myconf
set result [ 
	catch {
		eval $CELLDT_CPU_CONFIG		
		myconf config memory_size "${CELLDT_MEMORY_SIZE}M" 
	} errormessage
]
if { $result != 0 } {
	CellDT_Error "Could not create machine configuration." $errormessage
}
define machine myconf mysim
	
# Start the GUI if -g option was given
MamboInit::gui $env(LIB_DIR)/cell/gui/gui.tcl

# Construct the emulated device tree
build_firmware_tree

# Set boot parameters if desired
of::set_bootargs "lpj=8000000 console=hvc0 root=/dev/mambobd0 rw"

# Load the OS
set result [ catch { mysim mcm 0 load vmlinux $CELLDT_KERNEL_IMAGE 0x1000000  } errormessage ]
if { $result != 0 } {
	CellDT_Error "Could not set up operating system image." $errormessage
}

# Setup the root file system
set result [ 
	catch { 
		switch $CELLDT_ROOT_PERSISTENCE {
			# There is no option to mount the root file system as readonly.
			# This would be meaningless.
			"discard" {
				puts "mysim bogus disk init 0 $CELLDT_ROOT_IMAGE newcow $CELLDT_VAR_DIR/root_ignored 1024"
				mysim bogus disk init 0 $CELLDT_ROOT_IMAGE newcow $CELLDT_VAR_DIR/root_ignored 1024
			}
			"write" {
				puts "mysim bogus disk init 0 $CELLDT_ROOT_IMAGE rw"
				mysim bogus disk init 0 $CELLDT_ROOT_IMAGE rw
			}
			"journal" {
				puts "mysim bogus disk init 0 $CELLDT_ROOT_IMAGE cow $CELLDT_ROOT_JOURNAL 1024"
				mysim bogus disk init 0 $CELLDT_ROOT_IMAGE cow $CELLDT_ROOT_JOURNAL 1024
			}
		}
	} errormessage 
]
if { $result != 0 } {
	# (does not work)
	CellDT_Error "Could not set up root file system." $errormessage 
}

# Setup the alternative file system
if { $CELLDT_EXTRA_INIT } {
	set result [ 
		catch { 
			switch $CELLDT_EXTRA_PERSISTENCE {
				"readonly" {
					puts "mysim bogus disk init 1 $CELLDT_EXTRA_IMAGE r"
					mysim bogus disk init 1 $CELLDT_EXTRA_IMAGE r
				}
				"discard" {
					puts "mysim bogus disk init 1 $CELLDT_EXTRA_IMAGE newcow $CELLDT_VAR_DIR/extra_ignored 1024 "
					mysim bogus disk init 1 $CELLDT_EXTRA_IMAGE newcow $CELLDT_VAR_DIR/extra_ignored 1024 
				}
				"write" {
					puts "mysim bogus disk init 1 $CELLDT_EXTRA_IMAGE rw"
					mysim bogus disk init 1 $CELLDT_EXTRA_IMAGE rw
				}
				"journal" {
					puts "mysim bogus disk init 1 $CELLDT_EXTRA_IMAGE cow $CELLDT_EXTRA_JOURNAL 1024"
					mysim bogus disk init 1 $CELLDT_EXTRA_IMAGE cow $CELLDT_EXTRA_JOURNAL 1024
				}
			}
		} errormessage 
	]
	if { $result != 0 } {
		# (does not work)
		CellDT_Error "Could not set up alternative file system." $errormessage 
	}
}

# ---------------------------------------------------------------------------------------
# BOGUSNET:
# A new tap device must be created. The name of the device must
# be saved in a temporary file, so that it can be read by the 
# cleanup script and deallocated after systemsim exits.

if { $CELLDT_NET_INIT } {
	CellDT_Status "INIT" "Bogusnet"
		# Create the tun device.
	# snif will create the device and print its name to stdout.
	# The output is captured and saved into CELLDT_TAP_DEVICE.
	# If snif fails, CELLDT_TAP_DEVICE will contain the error message instead.
	set result [ 
		catch {
			puts "Exec: $SYSTEMSIM_TOP/bin/snif -c -u $CELLDT_USERID $CELLDT_NET_IP_HOST $CELLDT_NET_MASK"
			exec $SYSTEMSIM_TOP/bin/snif -c -u $CELLDT_USERID $CELLDT_NET_IP_HOST $CELLDT_NET_MASK
		} CELLDT_TAP_DEVICE
	]
	if { $result != 0 } {
		set error_message $CELLDT_TAP_DEVICE
		CellDT_Error "Failed to create tun/tap device." $errormessage
	}
	
	# Save the device name to the launch log so that the clean up script can
	# remove the tun/tap device again.
	set result [ 
		catch {
			exec echo $CELLDT_TAP_DEVICE > $CELLDT_VAR_DIR/TAP_DEVICE
		} error_message
	]
	if { $result != 0 } {
		exec $SYSTEMSIM_TOP/bin/snif -d $CELLDT_TAP_DEVICE
		CellDT_Error "Could not save configuration log in $CELLDT_VAR_DIR." $error_message
	}
	
	# Check if the device was created with proper permissions.
	puts "Allocated tun/tap = $CELLDT_TAP_DEVICE."
	if { ! [ file readable /dev/net/tun ] } {
		exec $SYSTEMSIM_TOP/bin/snif -d $CELLDT_TAP_DEVICE
		CellDT_Error "The device /dev/net/tun is not readable. Check udev rules."
	}
	if { ! [ file writable /dev/net/tun ] } {
		exec $SYSTEMSIM_TOP/bin/snif -d $CELLDT_TAP_DEVICE
		CellDT_Error "The device /dev/net/tun is not writable. Check udev rules."
	}

	# Finally, start bogusnet
	mysim bogus net init 0 $CELLDT_NET_MAC_SIMULATOR $CELLDT_TAP_DEVICE 0 0
}

# ---------------------------------------------------------------------------------------
# LINUX CONSOLE:
# Create a console redirected to a socket connected by CellDT.
	
if { $CELLDT_CONSOLE_INIT } {
	CellDT_Status "INIT" "Console"

	# Last parameter with value 0 means try socket connection, without being 
	# interfered by GUI that is launching on the same time.
	# Without this parameter, GUI will cancel the socket.	
	set result [ catch { mysim console create eclipse inout listen $CELLDT_CONSOLE_PORT 100 20  } errormessage ]

	if { $result != 0 } {
		CellDT_Error "Could not create console." $errormessage
		exit 1
	}
}
CellDT_Status "INIT" "Configured"

# ---------------------------------------------------------------------------------------
# PAUSE/RESUME TRIGGERS:
# Notify CellDT when simulator is started or stopped.

proc CellDT_Start { args } {
	CellDT_Status "SIMULATOR" "Start"
}

proc CellDT_Stop { args } {
	CellDT_Status "SIMULATOR" "Stop"
}

mysim trigger set assoc "SIM_START" CellDT_Start
mysim trigger set assoc "SIM_STOP" CellDT_Stop

# ---------------------------------------------------------------------------------------
# SHUT DOWN:
# Notify CellDT when simulator is being shutdown.
# This triggers are unset once activated

proc CellDT_ShutdownNotified { args } {
	array set triginfo $args
	mysim trigger clear console $triginfo(match)
	CellDT_Status "SHUTDOWN" "Prepared"
}

proc CellDT_ShutdownStarted { args } {
	array set triginfo $args
	mysim trigger clear console $triginfo(match)
	CellDT_Status "SHUTDOWN" "Started"
}

proc CellDT_ShutdownComplete { args } {
	array set triginfo $args
	mysim trigger clear console $triginfo(match)
	simstop
	CellDT_Status "SHUTDOWN" "Complete"
	quit
}

mysim trigger set console "The system is going down for system halt NOW!" CellDT_ShutdownNotified
mysim trigger set console "INIT: Switching to runlevel: 0" CellDT_ShutdownStarted
mysim trigger set console "INIT: no more processes left in this runlevel" CellDT_ShutdownComplete

proc writeConsole { t } {
	mysim console create console_id in string $t
}

# ---------------------------------------------------------------------------------------
# BOOT:
# Notify several steps during boot.
# After boot, pass run the configuration script, set configuration triggers.
# This triggers are unset once activated

proc CellDT_BootedBios { args } {
	array set triginfo $args
	mysim trigger clear console $triginfo(match)
	CellDT_Status "BOOT" "Linux"
}

proc CellDT_BootedLinux { args } {
	array set triginfo $args
	mysim trigger clear console $triginfo(match)
	CellDT_Status "BOOT" "System"
}

proc CellDT_BootReady { args } {
	array set triginfo $args
	mysim trigger clear console $triginfo(match)
	CellDT_DoCellDTConfiguration
}

proc CellDT_BootNearlyReady { args } {
	array set triginfo $args
	mysim trigger clear console $triginfo(match)
	mysim trigger set console "#" CellDT_BootReady
}

mysim trigger set console "Starting Linux" CellDT_BootedBios
mysim trigger set console "Welcome to Fedora Core" CellDT_BootedLinux
mysim trigger set console "INIT: Entering runlevel: 2" CellDT_BootNearlyReady

# ---------------------------------------------------------------------------------------
# CONFIGURATION:
# Do modifications on the default running Linux environment to:
# - Create an user to launch applications
# - Launch ssh server for remote connections
# This triggers are unset once activated

proc writeConsole { t } {
	mysim console create console_id in string "$t\n"
}

proc CellDT_NearlyConfigured { args } {
	array set triginfo $args
	mysim trigger clear console $triginfo(match)
	mysim trigger set console "#" CellDT_Configured
}
	
proc CellDT_Configured { args } {
	array set triginfo $args
	mysim trigger clear console $triginfo(match)
	CellDT_Status "BOOT" "Complete"
	# Don't pause anymore, since simulator will receive SSH connections.
	# simstop
}

proc CellDT_DoCellDTConfiguration { args } {
	CellDT_Status "BOOT" "Configure"
	global CELLDT_SSH_INIT
	global CELLDT_HAS_USER_CONFIG
	global CELLDT_NET_IP_HOST
	global CELLDT_NET_IP_SIMULATOR
	global CELLDT_NET_MASK
	global CELLDT_NET_INIT
	global CELLDT_CONSOLE_ECHO
	global CELLDT_WORKDIR
	global CELLDT_EXTRA_INIT
	global CELLDT_EXTRA_MOUNTPOINT
	global CELLDT_EXTRA_TYPE
	global CELLDT_EXTRA_PERSISTENCE
	global CELLDT_CONSOLE_COMMANDS

	mysim trigger set console "Configuration complete" CellDT_NearlyConfigured
	writeConsole "# Starting configuration"
	writeConsole "export CELLDT_WORKDIR=$CELLDT_WORKDIR"

	# Sets the simulator IP address and set environment variables with network configuration..
	if { $CELLDT_NET_INIT } {
		writeConsole "modprobe systemsim_net"
		writeConsole "ifconfig eth0 $CELLDT_NET_IP_SIMULATOR netmask $CELLDT_NET_MASK up"
		writeConsole "export CELLDT_NET_IP_SIMULATOR=$CELLDT_NET_IP_SIMULATOR"
		writeConsole "export CELLDT_NET_IP_HOST=$CELLDT_NET_IP_HOST"
		writeConsole "export CELLDT_NET_MASK=$CELLDT_NET_MASK"
	}

	# Upload required files. Actually, for performance reason, since writing
	# commands to the console is very slow, the script is called through
	# and the executed.
	if { $CELLDT_SSH_INIT } {
		writeConsole "cd /tmp/"
		writeConsole "callthru source \$CELLDT_WORKDIR/configure.sh > configure.sh"
		writeConsole "chmod u+x configure.sh"
		writeConsole "./configure.sh"
	}
	
	if { $CELLDT_EXTRA_INIT } {
		set mountswitch ""
		switch $CELLDT_EXTRA_PERSISTENCE {
			"discard" {
				set mountswitch "-w"
			}
			"write" {
				set mountswitch "-w"
			}
			"journal" {
				set mountswitch "-w"
			}
			"readonly" {
				set mountswitch "-r"
			}
		}
		writeConsole "mkdir -p $CELLDT_EXTRA_MOUNTPOINT"
		writeConsole "chmod a+wrx $CELLDT_EXTRA_MOUNTPOINT"
		switch $CELLDT_EXTRA_TYPE {
			"ext2" {
				puts "mount -t ext2 $mountswitch -o exec,suid /dev/mambobd1 $CELLDT_EXTRA_MOUNTPOINT"
				writeConsole "mount -t ext2 $mountswitch -o exec,suid /dev/mambobd1 $CELLDT_EXTRA_MOUNTPOINT"
			}
			"iso9660" {
				puts "mount -t iso9660 $mountswitch -o exec,uid=$(id -u user),gid=$(id -g user) /dev/mambobd1 $CELLDT_EXTRA_MOUNTPOINT"
				writeConsole "mount -t iso9660 $mountswitch -o exec,uid=$(id -u user),gid=$(id -g user) /dev/mambobd1 $CELLDT_EXTRA_MOUNTPOINT"
			}
		}
	}
	
	writeConsole $CELLDT_CONSOLE_COMMANDS

	if { ! $CELLDT_CONSOLE_ECHO } {
		writeConsole "stty -echo"
		writeConsole "echo 'Configuration complete'"
	} else {
		writeConsole "# Configuration complete"
	}
}

# ---------------------------------------------------------------------------------------
# RUN SIMULATOR
#for {set i 0} {$i < 8} {incr i} {
#	mysim spu $i set model fast
#}
mysim modify fast on
CellDT_Status "BOOT" "Bios"
mysim go

