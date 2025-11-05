# -*- coding: utf-8 -*-
"""
Roberto Valenti's Algebraic Quaterion Algorithm (AQUA) :cite:p:`valenti2015`
estimates a quaternion with the algebraic solution of a system from
inertial+magnetic observations, solving `Wahba's Problem
<https://en.wikipedia.org/wiki/Wahba%27s_problem>`_.

AQUA computes the "tilt" quaternion and the "heading" quaternion separately in
two sub-parts. This avoids the impact of the magnetic disturbances on the roll
and pitch components of the orientation.

AQUA can be used with a complementary filter to fuse the gyroscope data
together with accelerometer and magnetic field readings. The correction part of
the filter is based on the independently estimated quaternions and works for
both IMU (Inertial Measurement Unit) and MARG (Magnetic, Angular Rate, and
Gravity) sensors :cite:p:`valenti2016`.

"""

def estimate(self, acc: np.ndarray, mag: np.ndarray = None) -> np.ndarray:
    """
    Quaternion from Earth-Field Observations

    Algebraic estimation of a quaternion as a function of an observation of
    the Earth's gravitational force and magnetic fields.

    It decomposes the quaternion :math:`\\mathbf{q}` into two auxiliary
    quaternions :math:`\\mathbf{q}_{\\mathrm{acc}}` and
    :math:`\\mathbf{q}_{\\mathrm{mag}}`, such that:

    .. math::
        \\mathbf{q} = \\mathbf{q}_{\\mathrm{acc}}\\mathbf{q}_{\\mathrm{mag}}

    Parameters
    ----------
    acc : numpy.ndarray
        Sample of tri-axial Accelerometer in m/s^2
    mag : numpy.ndarray, default: None
        Sample of tri-axial Magnetometer in mT

    Returns
    -------
    q : numpy.ndarray
        Estimated quaternion.
    """
    self._assert_triaxial_sample_vector(acc, 'accelerometer')
    a_norm = np.linalg.norm(acc)
    if a_norm == 0:
        raise ValueError("Invalid acceleration data. It must contain at least one non-zero value.")
    ax, ay, az = acc/a_norm
    # Quaternion from Accelerometer Readings (eq. 25)
    if az >= 0:
        q_acc = np.array([np.sqrt((az+1)/2), -ay/np.sqrt(2*(az+1)), ax/np.sqrt(2*(az+1)), 0.0])
    else:
        q_acc = np.array([-ay/np.sqrt(2*(1-az)), np.sqrt((1-az)/2.0), 0.0, ax/np.sqrt(2*(1-az))])
    q_acc = Quaternion(q_acc)
    if mag is not None:
        self._assert_triaxial_sample_vector(mag, 'magnetometer')
        m_norm = np.linalg.norm(mag)
        if m_norm == 0:
            raise ValueError("Invalid geomagnetic field. Its must contain at least one non-zero value.")
        lx, ly, _ = q_acc.to_DCM().T @ (mag/m_norm) # (eq. 26)
        Gamma = lx**2 + ly**2                       # (eq. 28)
        # Quaternion from Magnetometer Readings (eq. 35)
        if lx >= 0:
            q_mag = np.array([np.sqrt(Gamma+lx*np.sqrt(Gamma))/np.sqrt(2*Gamma),
                                0.0,
                                0.0,
                                ly/(np.sqrt(2)*np.sqrt(Gamma+lx*np.sqrt(Gamma)))
                                ])
        else:
            q_mag = np.array([ly/(np.sqrt(2)*np.sqrt(Gamma-lx*np.sqrt(Gamma))),
                                0.0,
                                0.0,
                                np.sqrt(Gamma-lx*np.sqrt(Gamma))/np.sqrt(2*Gamma)
                                ])
        # Generalized Quaternion Orientation (eq. 36)
        q = q_acc.product(q_mag)
        return q/np.linalg.norm(q)
    return q_acc