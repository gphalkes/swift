import os
import re

TestDir='tests'

env = Environment(CPPPATH = ['.'])

env.SharedLibrary (
    target='p2tp',
    source = [ 'bin.cpp','hashtree.cpp','datagram.cpp',
               'sbit.cpp' ],
    LIBS=['stdc++','gtest','glog','crypto'] )

SConscript('tests/SConscript')
