from pathlib import Path

from setuptools import find_packages, setup

package_name = 'hand_control_description'


def collect_data_files(*directories):
    data_files = []

    for directory in directories:
        root = Path(directory)
        if not root.exists():
            continue

        files_by_parent = {}
        for path in root.rglob('*'):
            if path.is_file():
                files_by_parent.setdefault(path.parent, []).append(str(path))

        for parent, files in files_by_parent.items():
            install_dir = Path('share') / package_name / parent
            data_files.append((install_dir.as_posix(), files))

    return data_files


setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ] + collect_data_files('config', 'launch', 'models'),
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='plf',
    maintainer_email='maintainer@example.com',
    description='Model description project',
    license='Apache-2.0',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'hand_state_publisher = hand_control_description.hand_state_publisher:main'
        ],
    },
)
