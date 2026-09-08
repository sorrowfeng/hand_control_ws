from setuptools import find_packages, setup
from glob import glob

package_name = 'tactile_demo_py'

setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
        ('share/' + package_name + "/launch", glob('launch/*.launch.py')),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='plf',
    maintainer_email='maintainer@example.com',
    description='Hand control Python tactile sensor check demo',
    license='Apache-2.0',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'tactile_checker = tactile_demo_py.tactile_checker:main'
        ],
    },
)
