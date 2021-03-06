# This file is part of SM (Stream Manipulator).
#
# SM is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# SM is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with SM.  If not, see <http://www.gnu.org/licenses/>.

tnames = [
	'EOF',
	'UNKNOWN',
	'ID',
	'NUM',
	'STR',
	'REGEX',
	'SHELL'
]
for tname in tnames:
	locals()[tname] = tname
